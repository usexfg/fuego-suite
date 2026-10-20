// fci-oracle produces the fci_oracle.json that a Fuego node operator points
// --fci-oracle at. It runs outside consensus: it reads the public BLS data API,
// writes the six basket prices plus the headline CPI, and produces a file a
// human can read before the daemon picks it up.
//
// The daemon side is MinerConfig.cpp::tryLoadOracleJson, which scans the file
// for "key": number pairs. Six keys matter, in USD per FCI basket unit:
//
//	power  USD per kWh
//	milk   USD per gallon
//	bread  USD per kg of loaf
//	eggs   USD per dozen
//	gas    USD per gallon
//	rice   USD per kg uncooked
//
// Those six must stay unquoted numbers or the daemon's flat scanner skips
// them. Everything else in the file is metadata the daemon ignores.
//
// Sources are BLS only. A second vendor was considered and rejected: the
// candidate's US feed republishes BLS data, so averaging it against BLS would
// double-count one source rather than cross-check two.
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

const poundsPerKilogram = 2.20462262

type category struct {
	key       string
	unit      string
	blsSeries string
	perPound  bool
}

var categories = []category{
	{key: "power", unit: "USD/kWh", blsSeries: "APU000072610"},
	{key: "milk", unit: "USD/gal", blsSeries: "APU0000709112"},
	{key: "bread", unit: "USD/kg", blsSeries: "APU0000702111", perPound: true},
	{key: "eggs", unit: "USD/dozen", blsSeries: "APU0000708111"},
	{key: "gas", unit: "USD/gal", blsSeries: "APU00007471A"},
	{key: "rice", unit: "USD/kg", blsSeries: "APU0000701312", perPound: true},
}

// headlineCPISeries is the US CPI-U all-items NSA index — the single total
// inflation measure the Hearthstone tracks. The six basket series exist so
// miners can vote on the physical cost of living, not to measure inflation.
const headlineCPISeries = "CUUR0000SA0"

type observation struct {
	period string
	value  float64
}

type series []observation

func (s series) latestOnOrBefore(month string) (observation, bool) {
	var best observation
	found := false
	for _, p := range s {
		if p.period > month {
			continue
		}
		if !found || p.period > best.period {
			best, found = p, true
		}
	}
	return best, found
}

func periodOf(year, period string) string {
	return year + "-" + strings.TrimPrefix(period, "M")
}

func main() {
	out := flag.String("out", "fci_oracle.json", "path to write")
	blsKey := flag.String("bls-key", os.Getenv("BLS_REGISTRATION_KEY"), "BLS registration key; unregistered access is sufficient at this volume")
	timeout := flag.Duration("timeout", 30*time.Second, "per-request timeout")
	flag.Parse()

	client := &http.Client{Timeout: *timeout}
	report, err := build(client, *blsKey)
	if err != nil {
		fmt.Fprintln(os.Stderr, "fci-oracle:", err)
		os.Exit(1)
	}

	if dir := filepath.Dir(*out); dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			fmt.Fprintln(os.Stderr, "fci-oracle:", err)
			os.Exit(1)
		}
	}
	if err := os.WriteFile(*out, report, 0o644); err != nil {
		fmt.Fprintln(os.Stderr, "fci-oracle:", err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "fci-oracle: wrote %s\n", *out)
}

func build(client *http.Client, blsKey string) ([]byte, error) {
	ids := []string{headlineCPISeries}
	for _, c := range categories {
		ids = append(ids, c.blsSeries)
	}

	bls, err := fetchBLS(client, ids, blsKey)
	if err != nil {
		return nil, err
	}

	latest := ""
	for _, s := range bls {
		for _, p := range s {
			if p.period > latest {
				latest = p.period
			}
		}
	}
	if latest == "" {
		return nil, fmt.Errorf("BLS returned no monthly observations for %v", ids)
	}

	prices := map[string]float64{}
	notes := map[string]string{}
	provenance := map[string]any{}

	for _, c := range categories {
		now, ok := bls[c.blsSeries].latestOnOrBefore(latest)
		if !ok {
			notes[c.key] = "BLS returned no observation on or before " + latest
			continue
		}
		value := now.value
		if c.perPound {
			value *= poundsPerKilogram
		}
		prices[c.key] = value
		provenance[c.key+"_bls_series"] = c.blsSeries
		provenance[c.key+"_bls_period"] = now.period
		provenance[c.key+"_bls_value"] = now.value
		provenance[c.key+"_unit"] = c.unit
	}

	cpi, hasCPI := observation{}, false
	if s, ok := bls[headlineCPISeries]; ok {
		cpi, hasCPI = s.latestOnOrBefore(latest)
	}
	if hasCPI {
		provenance["headline_cpi_series"] = headlineCPISeries
		provenance["headline_cpi_period"] = cpi.period
		provenance["headline_cpi_index"] = cpi.value
	} else {
		notes["headline_cpi"] = fmt.Sprintf("no CPI-U observation on or before %s", latest)
	}

	if len(prices) == 0 {
		return nil, fmt.Errorf("no category produced a price")
	}

	keys := make([]string, 0, len(prices))
	for k := range prices {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	var buf bytes.Buffer
	buf.WriteString("{\n")
	for _, k := range keys {
		fmt.Fprintf(&buf, "  %q: %.4f,\n", k, prices[k])
	}
	fmt.Fprintf(&buf, "  \"_latest_period\": %q,\n", latest)
	fmt.Fprintf(&buf, "  \"_source\": \"BLS Public Data API v2\",\n")
	fmt.Fprintf(&buf, "  \"_notes\": %s,\n", mustJSON(notes))
	fmt.Fprintf(&buf, "  \"_provenance\": %s\n", mustJSON(provenance))
	buf.WriteString("}\n")

	return buf.Bytes(), nil
}

type blsResponse struct {
	Status  string `json:"status"`
	Message []any  `json:"message"`
	Results struct {
		Series []struct {
			SeriesID string `json:"seriesID"`
			Data     []struct {
				Year   string `json:"year"`
				Period string `json:"period"`
				Value  string `json:"value"`
			} `json:"data"`
		} `json:"series"`
	} `json:"Results"`
}

func fetchBLS(client *http.Client, ids []string, registrationKey string) (map[string]series, error) {
	body := map[string]any{"seriesid": ids}
	if registrationKey != "" {
		body["registrationkey"] = registrationKey
	}
	payload, err := json.Marshal(body)
	if err != nil {
		return nil, err
	}

	req, err := http.NewRequest(http.MethodPost, "https://api.bls.gov/publicAPI/v2/timeseries/data/", bytes.NewReader(payload))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("User-Agent", "FuegoSuite-FCIOracle/1.0 (https://github.com/usexfg/fuego-suite)")

	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("BLS request: %w", err)
	}
	defer resp.Body.Close()

	var decoded blsResponse
	if err := json.NewDecoder(resp.Body).Decode(&decoded); err != nil {
		return nil, fmt.Errorf("BLS decode: %w", err)
	}
	if decoded.Status != "REQUEST_SUCCEEDED" {
		return nil, fmt.Errorf("BLS status %s: %v", decoded.Status, decoded.Message)
	}

	out := make(map[string]series, len(decoded.Results.Series))
	for _, s := range decoded.Results.Series {
		var points series
		for _, d := range s.Data {
			if !strings.HasPrefix(d.Period, "M") {
				continue
			}
			v, err := strconv.ParseFloat(d.Value, 64)
			if err != nil {
				continue
			}
			points = append(points, observation{period: periodOf(d.Year, d.Period), value: v})
		}
		out[s.SeriesID] = points
	}
	return out, nil
}

func mustJSON(v any) string {
	b, err := json.Marshal(v)
	if err != nil {
		return "null"
	}
	return string(b)
}
