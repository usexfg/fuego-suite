package app

import (
	"encoding/json"
	"testing"
)

func TestSwapOfferParsing(t *testing.T) {
	raw := `{"offers":[{"offerId":"abc123","xfgAmount":80000000,"rateNum":22000,"pair":1,"makerPubKey":"deadbeef","timestamp":1711100000,"ttlBlocks":180,"postedHeight":184000}],"status":"OK"}`
	var resp struct {
		Offers []SwapOffer `json:"offers"`
		Status string      `json:"status"`
	}
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(resp.Offers) != 1 {
		t.Fatalf("expected 1 offer, got %d", len(resp.Offers))
	}
	o := resp.Offers[0]
	if o.OfferID != "abc123" {
		t.Errorf("offerId = %q, want abc123", o.OfferID)
	}
	if o.XfgAmount != 80000000 {
		t.Errorf("xfgAmount = %d, want 80000000", o.XfgAmount)
	}
	if o.Pair != 1 {
		t.Errorf("pair = %d, want 1", o.Pair)
	}
}

func TestSwapPriceParsing(t *testing.T) {
	raw := `{"twap":"22000.0","seedRate":"22000.0","compositeRate":"21500.5","sourceCount":3,"sources":[],"xfgUsdLow":"0.0080","xfgUsdHigh":"0.0120","xfgUsdMid":"0.0100","pairImplied":[],"status":"OK"}`
	var resp SwapPriceResponse
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.CompositeRate != "21500.5" {
		t.Errorf("compositeRate = %q, want 21500.5", resp.CompositeRate)
	}
	if resp.XfgUsdMid != "0.0100" {
		t.Errorf("xfgUsdMid = %q, want 0.0100", resp.XfgUsdMid)
	}
}

func TestSwapStatusParsing(t *testing.T) {
	raw := `{"swapId":"abc123","pair":0,"role":"BOB","state":"ESCROW_FUNDED","xfgAmount":8000000,"ctrAmount":500000,"rate":"80.0","escrowKey":"aabb","adaptorPoint":"ccdd","ourSwapKey":"1122","peerSwapKey":"3344","escrowTxHash":"eeff","ctrLockTxId":"","peerEndpoint":"192.168.1.10:20808","createdAt":1711100000,"updatedAt":1711100500}`
	var resp SwapStatus
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.XfgAmount != 8000000 {
		t.Errorf("xfgAmount = %d, want 8000000", resp.XfgAmount)
	}
	if resp.State != "ESCROW_FUNDED" {
		t.Errorf("state = %q, want ESCROW_FUNDED", resp.State)
	}
	if resp.EscrowKey != "aabb" {
		t.Errorf("escrowKey = %q, want aabb", resp.EscrowKey)
	}
	if resp.AdaptorPoint != "ccdd" {
		t.Errorf("adaptorPoint = %q, want ccdd", resp.AdaptorPoint)
	}
	if resp.Role != "BOB" {
		t.Errorf("role = %q, want BOB", resp.Role)
	}
}

func TestSwapTradeParsing(t *testing.T) {
	tests := []struct {
		name string
		json string
		pair uint8
		rate string
	}{
		{
			name: "ETH trade",
			json: `{"pair":1,"xfgAmount":50000000,"ctrAmount":100000,"rate":"2200.5","blockHeight":184500,"timestamp":1711200000}`,
			pair: PairETH,
			rate: "2200.5",
		},
		{
			name: "XMR trade",
			json: `{"pair":2,"xfgAmount":30000000,"ctrAmount":50000,"rate":"155.0","blockHeight":184600,"timestamp":1711201000}`,
			pair: PairXMR,
			rate: "155.0",
		},
		{
			name: "SOL trade",
			json: `{"pair":0,"xfgAmount":10000000,"ctrAmount":20000,"rate":"80.0","blockHeight":184700,"timestamp":1711202000}`,
			pair: PairSOL,
			rate: "80.0",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var trade SwapTrade
			if err := json.Unmarshal([]byte(tt.json), &trade); err != nil {
				t.Fatalf("unmarshal: %v", err)
			}
			if trade.Pair != tt.pair {
				t.Errorf("pair = %d, want %d", trade.Pair, tt.pair)
			}
			if trade.Rate != tt.rate {
				t.Errorf("rate = %q, want %q", trade.Rate, tt.rate)
			}
		})
	}
}

func TestNodeInfoParsing(t *testing.T) {
	raw := `{"height":184500,"difficulty":123456789,"tx_count":50000,"tx_pool_size":3,"incoming_connections_count":5,"outgoing_connections_count":8,"version":"10.0.1","status":"OK"}`
	var info NodeInfo
	if err := json.Unmarshal([]byte(raw), &info); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if info.Height != 184500 {
		t.Errorf("height = %d, want 184500", info.Height)
	}
	if info.TxPoolSize != 3 {
		t.Errorf("txPoolSize = %d, want 3", info.TxPoolSize)
	}
	if info.IncomingConns != 5 {
		t.Errorf("incomingConns = %d, want 5", info.IncomingConns)
	}
	if info.OutgoingConns != 8 {
		t.Errorf("outgoingConns = %d, want 8", info.OutgoingConns)
	}
}

func TestTradesResponseParsing(t *testing.T) {
	raw := `{"trades":[{"pair":1,"xfgAmount":50000000,"ctrAmount":100000,"rate":"2200.5","blockHeight":184500,"timestamp":1711200000},{"pair":1,"xfgAmount":30000000,"ctrAmount":60000,"rate":"2201.0","blockHeight":184501,"timestamp":1711200100}],"status":"OK"}`
	var resp struct {
		Trades []SwapTrade `json:"trades"`
		Status string      `json:"status"`
	}
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(resp.Trades) != 2 {
		t.Fatalf("expected 2 trades, got %d", len(resp.Trades))
	}
	if resp.Trades[0].XfgAmount != 50000000 {
		t.Errorf("trade[0].xfgAmount = %d, want 50000000", resp.Trades[0].XfgAmount)
	}
	if resp.Trades[1].Rate != "2201.0" {
		t.Errorf("trade[1].rate = %q, want 2201.0", resp.Trades[1].Rate)
	}
}
