package app

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"
)

func RunHeadless(cfg Config) error {
	if cfg.WalletRPC == "" {
		return fmt.Errorf("--wallet is required for headless mode")
	}

	client := NewFuegoClient(cfg.DaemonRPC)
	var wallet *WalletClient
	if cfg.WalletUser != "" {
		wallet = NewWalletClientAuth(cfg.WalletRPC, cfg.WalletUser, cfg.WalletPass)
	} else {
		wallet = NewWalletClient(cfg.WalletRPC)
	}

	if !wallet.IsConnected() {
		return fmt.Errorf("cannot connect to wallet at %s", cfg.WalletRPC)
	}

	addr, err := wallet.GetAddress()
	if err != nil {
		return fmt.Errorf("wallet address: %w", err)
	}
	if len(addr) < 16 {
		return fmt.Errorf("wallet address too short: %q", addr)
	}
	log.Printf("wallet connected: %s...%s", addr[:12], addr[len(addr)-8:])

	info, err := client.GetInfo()
	if err != nil {
		log.Printf("WARNING: daemon unreachable at %s: %v", cfg.DaemonRPC, err)
	} else {
		log.Printf("daemon connected: height=%d", info.Height)
	}

	mux := http.NewServeMux()

	writeStatus := func(w http.ResponseWriter) {
		bal, _ := wallet.GetBalance()
		var totalOffers int
		for _, p := range ActivePairs {
			offers, _ := client.GetOffers(p)
			totalOffers += len(offers)
		}
		resp := map[string]interface{}{
			"status":    "ok",
			"wallet":    addr[:12] + "...",
			"connected": true,
			"offers":    totalOffers,
		}
		if bal != nil {
			resp["available"] = FormatBalance(bal.Available)
			resp["locked"] = FormatBalance(bal.Locked)
		}
		if info != nil {
			resp["height"] = info.Height
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}

	// /status — control API (headless)
	mux.HandleFunc("/status", func(w http.ResponseWriter, r *http.Request) {
		writeStatus(w)
	})
	// /health — compatibility with wallet DaemonManager probes
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		writeStatus(w)
	})

	mux.HandleFunc("/offer", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "POST only", http.StatusMethodNotAllowed)
			return
		}
		var req struct {
			AmountXfg  string `json:"amountXfg"`
			AmountCtr  string `json:"amountCtr"`
			Pair       string `json:"pair"`
			TimeoutHrs uint32 `json:"timeoutHrs"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		xfgAtomic, err := parseAmountAtomic(req.AmountXfg, 1e7)
		if err != nil {
			http.Error(w, "invalid amountXfg: "+err.Error(), http.StatusBadRequest)
			return
		}
		pairID := PairFromString(req.Pair)
		if pairID == 255 {
			http.Error(w, "unknown pair", http.StatusBadRequest)
			return
		}
		ctrFloat, _ := strconv.ParseFloat(req.AmountCtr, 64)
		if ctrFloat <= 0 {
			http.Error(w, "invalid amountCtr", http.StatusBadRequest)
			return
		}
		rateFloat := (float64(xfgAtomic) / 1e7) / ctrFloat
		rateNum := uint64(rateFloat * 1e7)
		ttlBlocks := req.TimeoutHrs * 30

		signed, err := wallet.SignOffer(xfgAtomic, rateNum, pairID, ttlBlocks, true)
		if err != nil {
			http.Error(w, "sign_offer: "+err.Error(), http.StatusInternalServerError)
			return
		}

		offerReq := map[string]interface{}{
			"offerId":     signed.OfferID,
			"xfgAmount":   xfgAtomic,
			"rateNum":     rateNum,
			"pair":        pairID,
			"makerPubKey": signed.MakerPubKey,
			"signature":   signed.Signature,
			"ttlBlocks":   ttlBlocks,
			"isSoftOrder": true,
		}
		var submitResp struct {
			Status string `json:"status"`
		}
		if err := client.post("/submitswap", offerReq, &submitResp); err != nil {
			http.Error(w, "submit: "+err.Error(), http.StatusInternalServerError)
			return
		}

		log.Printf("soft order posted: %s", signed.OfferID[:12])
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"offerId": signed.OfferID,
			"status":  "posted",
		})
	})

	mux.HandleFunc("/cancel", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "POST only", http.StatusMethodNotAllowed)
			return
		}
		var req struct {
			OfferID string `json:"offerId"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		signed, err := wallet.SignCancel(req.OfferID)
		if err != nil {
			http.Error(w, "sign_cancel: "+err.Error(), http.StatusInternalServerError)
			return
		}
		if err := client.CancelSwapOffer(signed.OfferID, signed.MakerPubKey, signed.Signature); err != nil {
			http.Error(w, "cancel: "+err.Error(), http.StatusInternalServerError)
			return
		}

		log.Printf("offer cancelled: %s", req.OfferID[:min(12, len(req.OfferID))])
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "cancelled"})
	})

	mux.HandleFunc("/offers", func(w http.ResponseWriter, r *http.Request) {
		all := make(map[string][]SwapOffer)
		for _, p := range ActivePairs {
			offers, err := client.GetOffers(p)
			if err == nil {
				all[PairShort(p)] = offers
			}
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(all)
	})

	srv := &http.Server{
		Addr:    fmt.Sprintf("127.0.0.1:%d", cfg.HeadlessPort),
		Handler: mux,
	}

	go func() {
		log.Printf("control API listening on %s", srv.Addr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("control API error: %v", err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	log.Printf("headless mode active — soft orders will auto-execute when taken")

	for {
		select {
		case sig := <-sigCh:
			log.Printf("received %s, shutting down", sig)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			srv.Shutdown(ctx)
			cancel()
			return nil
		case <-ticker.C:
			if !wallet.IsConnected() {
				log.Printf("WARNING: wallet disconnected")
				continue
			}
			bal, err := wallet.GetBalance()
			if err != nil {
				log.Printf("balance check failed: %v", err)
				continue
			}
			var totalOffers int
			for _, p := range ActivePairs {
				offers, _ := client.GetOffers(p)
				totalOffers += len(offers)
			}
			log.Printf("balance=%s XFG (locked=%s) | offers=%d",
				FormatBalance(bal.Available), FormatBalance(bal.Locked), totalOffers)
		}
	}
}
