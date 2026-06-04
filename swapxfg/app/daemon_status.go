package app

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"time"
)

type DaemonOffer struct {
	OfferId      string `json:"offerId"`
	Pair         int    `json:"pair"`
	XfgAmount    uint64 `json:"xfgAmount"`
	FilledAmount uint64 `json:"filledAmount"`
	RateNum      uint64 `json:"rateNum"`
	PostedHeight int    `json:"postedHeight"`
}

type DaemonSwap struct {
	SwapId        string `json:"swapId"`
	State         string `json:"state"`
	Pair          int    `json:"pair"`
	TimeoutHeight uint64 `json:"timeoutHeight"`
}

type DaemonStatus struct {
	Height int            `json:"height"`
	Offers []DaemonOffer  `json:"offers"`
	Swaps  []DaemonSwap   `json:"swaps"`
}

func FetchDaemonStatus(addr string) (*DaemonStatus, error) {
	conn, err := net.DialTimeout("tcp", addr, 3*time.Second)
	if err != nil {
		return nil, err
	}
	defer conn.Close()

	conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	data, err := io.ReadAll(conn)
	if err != nil {
		return nil, err
	}

	var status DaemonStatus
	if err := json.Unmarshal(data, &status); err != nil {
		return nil, fmt.Errorf("daemon status parse: %w", err)
	}
	return &status, nil
}
