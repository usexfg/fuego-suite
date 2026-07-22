package app

import (
	"encoding/json"
	"testing"
)

func TestOrderBookSnapshotParsing(t *testing.T) {
	raw := `{"bids":[{"price":15000000,"amount":5000,"orderCount":3},{"price":14000000,"amount":8000,"orderCount":2}],"asks":[{"price":16000000,"amount":3000,"orderCount":1},{"price":17000000,"amount":7000,"orderCount":4}],"spread":1000000,"height":184500,"status":"OK"}`
	var snap OrderBookSnapshot
	if err := json.Unmarshal([]byte(raw), &snap); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(snap.Bids) != 2 {
		t.Fatalf("expected 2 bid levels, got %d", len(snap.Bids))
	}
	if snap.Bids[0].Price != 15000000 {
		t.Errorf("bids[0].price = %d, want 15000000", snap.Bids[0].Price)
	}
	if snap.Bids[0].Amount != 5000 {
		t.Errorf("bids[0].amount = %d, want 5000", snap.Bids[0].Amount)
	}
	if snap.Bids[0].OrderCount != 3 {
		t.Errorf("bids[0].orderCount = %d, want 3", snap.Bids[0].OrderCount)
	}
	if len(snap.Asks) != 2 {
		t.Fatalf("expected 2 ask levels, got %d", len(snap.Asks))
	}
	if snap.Asks[0].Price != 16000000 {
		t.Errorf("asks[0].price = %d, want 16000000", snap.Asks[0].Price)
	}
	if snap.Spread != 1000000 {
		t.Errorf("spread = %d, want 1000000", snap.Spread)
	}
	if snap.Height != 184500 {
		t.Errorf("height = %d, want 184500", snap.Height)
	}
}

func TestOrderBookSnapshotEmpty(t *testing.T) {
	raw := `{"bids":[],"asks":[],"spread":0,"height":0,"status":"OK"}`
	var snap OrderBookSnapshot
	if err := json.Unmarshal([]byte(raw), &snap); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(snap.Bids) != 0 {
		t.Errorf("expected 0 bids, got %d", len(snap.Bids))
	}
	if len(snap.Asks) != 0 {
		t.Errorf("expected 0 asks, got %d", len(snap.Asks))
	}
}

func TestPlaceOrderResultParsing(t *testing.T) {
	raw := `{"orderId":"abc123def456","status":"OK","filled":500,"statusMsg":"Order placed"}`
	var resp PlaceOrderResult
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.OrderId != "abc123def456" {
		t.Errorf("orderId = %q, want abc123def456", resp.OrderId)
	}
	if resp.Filled != 500 {
		t.Errorf("filled = %d, want 500", resp.Filled)
	}
	if resp.StatusMsg != "Order placed" {
		t.Errorf("statusMsg = %q, want 'Order placed'", resp.StatusMsg)
	}
}

func TestOpenOrderParsing(t *testing.T) {
	raw := `{"orders":[{"orderId":"ord123","side":"BID","pair":0,"price":15000000,"amount":1000,"filled":200,"timestamp":1711200000,"ttlBlocks":1440}],"status":"OK"}`
	var resp struct {
		Orders []OpenOrder `json:"orders"`
		Status string      `json:"status"`
	}
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(resp.Orders) != 1 {
		t.Fatalf("expected 1 order, got %d", len(resp.Orders))
	}
	o := resp.Orders[0]
	if o.OrderId != "ord123" {
		t.Errorf("orderId = %q, want ord123", o.OrderId)
	}
	if o.Side != "BID" {
		t.Errorf("side = %q, want BID", o.Side)
	}
	if o.Pair != 0 {
		t.Errorf("pair = %d, want 0", o.Pair)
	}
	if o.Price != 15000000 {
		t.Errorf("price = %d, want 15000000", o.Price)
	}
	if o.Amount != 1000 {
		t.Errorf("amount = %d, want 1000", o.Amount)
	}
	if o.Filled != 200 {
		t.Errorf("filled = %d, want 200", o.Filled)
	}
}

func TestRenderOrderbookNil(t *testing.T) {
	result := RenderOrderbook(nil, 40, 20, false)
	if result == "" {
		t.Error("expected non-empty result for nil book")
	}
}

func TestRenderOrderbookEmpty(t *testing.T) {
	book := &OrderBookSnapshot{
		Bids: []OrderBookLevel{},
		Asks: []OrderBookLevel{},
	}
	result := RenderOrderbook(book, 40, 20, false)
	if result == "" {
		t.Error("expected non-empty result for empty book")
	}
}

func TestRenderOrderbookWithLevels(t *testing.T) {
	book := &OrderBookSnapshot{
		Bids: []OrderBookLevel{
			{Price: 15000000, Amount: 5000, OrderCount: 2},
			{Price: 14000000, Amount: 8000, OrderCount: 3},
		},
		Asks: []OrderBookLevel{
			{Price: 16000000, Amount: 3000, OrderCount: 1},
			{Price: 17000000, Amount: 7000, OrderCount: 4},
		},
		Spread: 1000000,
		Height: 184500,
	}
	result := RenderOrderbook(book, 60, 20, false)
	if result == "" {
		t.Error("expected non-empty result")
	}
}

func TestOrderEntryModelDefaults(t *testing.T) {
	m := newOrderEntryModel()
	if m.active {
		t.Error("expected inactive by default")
	}
	if m.side != 0 {
		t.Errorf("default side = %d, want 0", m.side)
	}
}

func TestOrderEntryModelOpenClose(t *testing.T) {
	m := newOrderEntryModel()
	m.open(2, 0)
	if !m.active {
		t.Error("expected active after open")
	}
	if m.pair != 2 {
		t.Errorf("pair = %d, want 2", m.pair)
	}
	if m.ttl != "1440" {
		t.Errorf("ttl = %q, want 1440", m.ttl)
	}
	m.close()
	if m.active {
		t.Error("expected inactive after close")
	}
}

func TestOrderEntryParsePrice(t *testing.T) {
	m := newOrderEntryModel()
	m.price = "1.5"
	price, ok := m.ParsePrice()
	if !ok {
		t.Fatal("expected ok")
	}
	expected := uint64(1.5 * priceDivisor)
	if price != expected {
		t.Errorf("price = %d, want %d", price, expected)
	}
}

func TestOrderEntryParseAmount(t *testing.T) {
	m := newOrderEntryModel()
	m.amount = "100.0"
	amt, ok := m.ParseAmount()
	if !ok {
		t.Fatal("expected ok")
	}
	expected := uint64(100.0 * priceDivisor)
	if amt != expected {
		t.Errorf("amount = %d, want %d", amt, expected)
	}
}

func TestOrderEntryParsePriceEmpty(t *testing.T) {
	m := newOrderEntryModel()
	_, ok := m.ParsePrice()
	if ok {
		t.Error("expected not ok for empty price")
	}
}

func TestOrderEntryParseAmountZero(t *testing.T) {
	m := newOrderEntryModel()
	m.amount = "0"
	_, ok := m.ParseAmount()
	if ok {
		t.Error("expected not ok for zero amount")
	}
}

func TestSignOrderResultParsing(t *testing.T) {
	raw := `{"orderId":"abc123","makerPubKey":"deadbeef","signature":"cafebabe"}`
	var resp SignOrderResult
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.OrderId != "abc123" {
		t.Errorf("orderId = %q, want abc123", resp.OrderId)
	}
	if resp.MakerPubKey != "deadbeef" {
		t.Errorf("makerPubKey = %q, want deadbeef", resp.MakerPubKey)
	}
	if resp.Signature != "cafebabe" {
		t.Errorf("signature = %q, want cafebabe", resp.Signature)
	}
}
