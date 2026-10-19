package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/big"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const xfgDecimals = 7

type SwapChain struct {
	ID             int    `json:"id"`
	Key            string `json:"key"`
	Symbol         string `json:"symbol"`
	AssetTicker    string `json:"assetTicker"`
	Name           string `json:"name"`
	Family         string `json:"family"`
	ChainID        uint64 `json:"chainId"`
	Decimals       int    `json:"decimals"`
	Implementation string `json:"implementation"`
	Protocol       bool   `json:"protocol"`
	Configured     bool   `json:"configured"`
	Ready          bool   `json:"ready"`
	ReadinessError string `json:"readinessError"`
}

type SwapDaemonStatus struct {
	Network string `json:"network"`
	Profile string `json:"profile"`
}

type SwapInitiateRequest struct {
	Pair               string `json:"pair"`
	Role               string `json:"role"`
	XFGAmount          string `json:"xfg_amount"`
	CounterpartyAmount string `json:"ctr_amount"`
	Peer               string `json:"peer"`
	ExpectedPeerKey    string `json:"expected_peer_pubkey"`
}

type SwapInitiateResult struct {
	SwapID        string `json:"swap_id"`
	OurSwapPubKey string `json:"our_swap_pubkey"`
}

type SwapActionResult struct {
	Success bool   `json:"success"`
	SwapID  string `json:"swap_id"`
	State   string `json:"state"`
}

type SwapDaemonClient struct {
	rpcURL          string
	token           string
	expectedNetwork string
	httpClient      *http.Client
}

type swapRPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

func (e *swapRPCError) Error() string {
	return fmt.Sprintf("swap daemon RPC %d: %s", e.Code, e.Message)
}

type swapRPCResponse struct {
	Result json.RawMessage `json:"result"`
	Error  *swapRPCError   `json:"error"`
}

func NewSwapDaemonClient(port int, token, expectedNetwork string) *SwapDaemonClient {
	return &SwapDaemonClient{
		rpcURL:          fmt.Sprintf("http://127.0.0.1:%d/", port),
		token:           token,
		expectedNetwork: expectedNetwork,
		httpClient: &http.Client{
			Timeout: 10 * time.Second,
			CheckRedirect: func(_ *http.Request, _ []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
	}
}

func validateSwapControlToken(token string) (string, error) {
	token = strings.TrimRight(token, "\r\n")
	if len(token) < 32 || len(token) > 256 {
		return "", errors.New("swap daemon control token must contain 32 to 256 characters")
	}
	for _, character := range token {
		if (character < 'a' || character > 'z') &&
			(character < 'A' || character > 'Z') &&
			(character < '0' || character > '9') &&
			character != '-' && character != '_' {
			return "", errors.New("swap daemon control token contains an invalid character")
		}
	}
	return token, nil
}

func (c *SwapDaemonClient) requireControlToken() error {
	if c.token == "" {
		return errors.New("swap daemon control token is required for mutations")
	}
	return nil
}

func (c *SwapDaemonClient) call(ctx context.Context, method string, params any, result any, mutation bool) error {
	if mutation {
		if err := c.requireControlToken(); err != nil {
			return err
		}
	}

	body, err := json.Marshal(map[string]any{
		"jsonrpc": "2.0",
		"id":      1,
		"method":  method,
		"params":  params,
	})
	if err != nil {
		return err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.rpcURL, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	if c.token != "" {
		req.Header.Set("X-Swap-Token", c.token)
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return mutationOutcomeError(method, err, mutation)
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusUnauthorized || resp.StatusCode == http.StatusForbidden {
		return fmt.Errorf("swap daemon rejected %s: HTTP %d", method, resp.StatusCode)
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return mutationOutcomeError(method, fmt.Errorf("HTTP %d", resp.StatusCode), mutation)
	}

	decoder := json.NewDecoder(io.LimitReader(resp.Body, 4<<20))
	decoder.UseNumber()
	var envelope swapRPCResponse
	if err := decoder.Decode(&envelope); err != nil {
		return mutationOutcomeError(method, err, mutation)
	}
	if envelope.Error != nil {
		return envelope.Error
	}
	if len(envelope.Result) == 0 || string(envelope.Result) == "null" {
		return mutationOutcomeError(method, errors.New("empty RPC result"), mutation)
	}
	if result == nil {
		return nil
	}
	decoder = json.NewDecoder(bytes.NewReader(envelope.Result))
	decoder.UseNumber()
	if err := decoder.Decode(result); err != nil {
		return mutationOutcomeError(method, err, mutation)
	}
	return nil
}

func mutationOutcomeError(method string, cause error, mutation bool) error {
	if !mutation {
		return fmt.Errorf("%s failed: %w", method, cause)
	}
	return fmt.Errorf("%s response failed; outcome is unknown, inspect swap status before retrying: %w", method, cause)
}

func (c *SwapDaemonClient) verifyContext(ctx context.Context) error {
	var status SwapDaemonStatus
	if err := c.call(ctx, "status", map[string]any{}, &status, false); err != nil {
		return err
	}
	if status.Network != c.expectedNetwork || status.Profile != c.expectedNetwork {
		return fmt.Errorf("swap daemon context mismatch: expected %s/%s, got %s/%s",
			c.expectedNetwork, c.expectedNetwork, status.Network, status.Profile)
	}
	return nil
}

func (c *SwapDaemonClient) ListReadyChains(ctx context.Context) ([]SwapChain, error) {
	if err := c.verifyContext(ctx); err != nil {
		return nil, err
	}
	var response struct {
		Chains []SwapChain `json:"chains"`
	}
	if err := c.call(ctx, "list_chains", map[string]any{}, &response, false); err != nil {
		return nil, err
	}
	ready := make([]SwapChain, 0, len(response.Chains))
	for _, chain := range response.Chains {
		if chain.Protocol && chain.Configured && chain.Ready {
			ready = append(ready, chain)
		}
	}
	if len(ready) == 0 {
		return nil, errors.New("swap daemon reports no protocol-ready chains")
	}
	return ready, nil
}

func (c *SwapDaemonClient) Initiate(ctx context.Context, request SwapInitiateRequest) (SwapInitiateResult, error) {
	if err := c.requireControlToken(); err != nil {
		return SwapInitiateResult{}, err
	}
	if request.Role != "alice" && request.Role != "bob" {
		return SwapInitiateResult{}, errors.New("swap role must be alice or bob")
	}
	if err := validateAtomicAmount(request.XFGAmount, new(big.Int).SetUint64(^uint64(0))); err != nil {
		return SwapInitiateResult{}, fmt.Errorf("invalid XFG amount: %w", err)
	}
	maxCounterpartyAmount := new(big.Int).Sub(new(big.Int).Lsh(big.NewInt(1), 256), big.NewInt(1))
	if err := validateAtomicAmount(request.CounterpartyAmount, maxCounterpartyAmount); err != nil {
		return SwapInitiateResult{}, fmt.Errorf("invalid counterparty amount: %w", err)
	}
	if err := validatePeerEndpoint(request.Peer); err != nil {
		return SwapInitiateResult{}, err
	}
	if err := validateExpectedPeerKey(request.ExpectedPeerKey); err != nil {
		return SwapInitiateResult{}, err
	}
	chains, err := c.ListReadyChains(ctx)
	if err != nil {
		return SwapInitiateResult{}, err
	}
	ready := false
	for _, chain := range chains {
		if chain.Key == request.Pair {
			ready = true
			break
		}
	}
	if !ready {
		return SwapInitiateResult{}, fmt.Errorf("chain %s is not protocol-ready", request.Pair)
	}
	var result SwapInitiateResult
	err = c.call(ctx, "initiate_swap", request, &result, true)
	return result, err
}

func (c *SwapDaemonClient) ListSwaps(ctx context.Context) ([]map[string]any, error) {
	if err := c.verifyContext(ctx); err != nil {
		return nil, err
	}
	var response struct {
		Swaps []map[string]any `json:"swaps"`
	}
	if err := c.call(ctx, "list_swaps", map[string]any{}, &response, false); err != nil {
		return nil, err
	}
	return response.Swaps, nil
}

func (c *SwapDaemonClient) SwapStatus(ctx context.Context, swapID string) (map[string]any, error) {
	if err := validateSwapID(swapID); err != nil {
		return nil, err
	}
	if err := c.verifyContext(ctx); err != nil {
		return nil, err
	}
	var response struct {
		Swap map[string]any `json:"swap"`
	}
	if err := c.call(ctx, "swap_status", map[string]string{"swap_id": swapID}, &response, false); err != nil {
		return nil, err
	}
	return response.Swap, nil
}

func (c *SwapDaemonClient) Accept(ctx context.Context, swapID string) (SwapActionResult, error) {
	return c.runAction(ctx, "accept", swapID)
}

func (c *SwapDaemonClient) Refund(ctx context.Context, swapID string) (SwapActionResult, error) {
	return c.runAction(ctx, "refund", swapID)
}

func (c *SwapDaemonClient) runAction(ctx context.Context, method, swapID string) (SwapActionResult, error) {
	if err := c.requireControlToken(); err != nil {
		return SwapActionResult{}, err
	}
	if err := validateSwapID(swapID); err != nil {
		return SwapActionResult{}, err
	}
	if err := c.verifyContext(ctx); err != nil {
		return SwapActionResult{}, err
	}
	var result SwapActionResult
	err := c.call(ctx, method, map[string]string{"swap_id": swapID}, &result, true)
	return result, err
}

func parseDisplayAmount(text string, decimals int, max *big.Int) (string, error) {
	if decimals < 0 || decimals > 77 {
		return "", errors.New("unsupported decimal precision")
	}
	if text == "" || strings.TrimSpace(text) != text || strings.HasPrefix(text, "+") || strings.HasPrefix(text, "-") {
		return "", errors.New("amount must be an unsigned decimal without surrounding whitespace")
	}
	parts := strings.Split(text, ".")
	if len(parts) > 2 || parts[0] == "" {
		return "", errors.New("invalid decimal amount")
	}
	if !decimalDigits(parts[0]) {
		return "", errors.New("invalid decimal amount")
	}
	fraction := ""
	if len(parts) == 2 {
		fraction = parts[1]
		if fraction == "" || !decimalDigits(fraction) || len(fraction) > decimals {
			return "", fmt.Errorf("amount supports at most %d decimal places", decimals)
		}
	}
	atomicText := strings.TrimLeft(parts[0]+fraction+strings.Repeat("0", decimals-len(fraction)), "0")
	if atomicText == "" {
		return "", errors.New("amount must be greater than zero")
	}
	atomic := new(big.Int)
	if _, ok := atomic.SetString(atomicText, 10); !ok || atomic.Sign() <= 0 {
		return "", errors.New("invalid decimal amount")
	}
	if max != nil && atomic.Cmp(max) > 0 {
		return "", errors.New("amount exceeds protocol range")
	}
	return atomic.String(), nil
}

func parseXFGAmount(text string) (string, error) {
	max := new(big.Int).SetUint64(^uint64(0))
	return parseDisplayAmount(text, xfgDecimals, max)
}

func parseCounterpartyAmount(text string, decimals int) (string, error) {
	max := new(big.Int).Sub(new(big.Int).Lsh(big.NewInt(1), 256), big.NewInt(1))
	return parseDisplayAmount(text, decimals, max)
}

func validateAtomicAmount(value string, max *big.Int) error {
	if !decimalDigits(value) {
		return errors.New("amount must be an unsigned decimal string")
	}
	amount := new(big.Int)
	if _, ok := amount.SetString(value, 10); !ok || amount.Sign() <= 0 {
		return errors.New("amount must be greater than zero")
	}
	if max != nil && amount.Cmp(max) > 0 {
		return errors.New("amount exceeds protocol range")
	}
	return nil
}

func decimalDigits(text string) bool {
	if text == "" {
		return false
	}
	for _, char := range text {
		if char < '0' || char > '9' {
			return false
		}
	}
	return true
}

func formatAtomicAmount(atomicText string, decimals int) string {
	if !decimalDigits(atomicText) || decimals <= 0 {
		return atomicText
	}
	if len(atomicText) <= decimals {
		atomicText = strings.Repeat("0", decimals-len(atomicText)+1) + atomicText
	}
	whole := atomicText[:len(atomicText)-decimals]
	fraction := strings.TrimRight(atomicText[len(atomicText)-decimals:], "0")
	if fraction == "" {
		return whole
	}
	return whole + "." + fraction
}

func validatePeerEndpoint(endpoint string) error {
	if endpoint == "" || strings.ContainsAny(endpoint, " \t\r\n/") {
		return errors.New("peer endpoint must be host:port")
	}
	colon := strings.LastIndexByte(endpoint, ':')
	if colon <= 0 || colon == len(endpoint)-1 || strings.Contains(endpoint[:colon], ":") {
		return errors.New("peer endpoint must be IPv4, DNS, or onion host:port")
	}
	port, err := strconv.Atoi(endpoint[colon+1:])
	if err != nil || port < 1 || port > 65535 {
		return errors.New("peer endpoint port must be 1-65535")
	}
	return nil
}

func validateExpectedPeerKey(key string) error {
	decoded, err := hex.DecodeString(key)
	if err != nil || len(decoded) != 32 {
		return errors.New("expected peer key must be 64 hexadecimal characters")
	}
	allZero := true
	for _, value := range decoded {
		if value != 0 {
			allZero = false
			break
		}
	}
	if allZero {
		return errors.New("expected peer key cannot be zero")
	}
	return nil
}

func validateSwapID(swapID string) error {
	if swapID == "" || len(swapID) > 128 {
		return errors.New("swap ID must contain 1-128 ASCII letters, digits, periods, colons, hyphens, or underscores")
	}
	for _, char := range swapID {
		if (char >= 'a' && char <= 'z') || (char >= 'A' && char <= 'Z') ||
			(char >= '0' && char <= '9') || char == '.' || char == ':' ||
			char == '-' || char == '_' {
			continue
		}
		return errors.New("swap ID must contain 1-128 ASCII letters, digits, periods, colons, hyphens, or underscores")
	}
	return nil
}
