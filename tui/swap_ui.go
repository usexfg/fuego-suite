package main

import (
	"context"
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type swapChainsMsg struct {
	chains []SwapChain
	err    error
}

type swapListMsg struct {
	swaps []map[string]any
	err   error
}

type swapStatusMsg struct {
	swap map[string]any
	err  error
}

type swapMutationMsg struct {
	method       string
	swapID       string
	state        string
	ourPublicKey string
	err          error
}

func expectedSwapNetwork(testnet bool) string {
	if testnet {
		return "testnet"
	}
	return "mainnet"
}

func swapCommandContext() (context.Context, context.CancelFunc) {
	return context.WithTimeout(context.Background(), 15*time.Second)
}

func loadSwapChainsCmd(client *SwapDaemonClient) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := swapCommandContext()
		defer cancel()
		chains, err := client.ListReadyChains(ctx)
		return swapChainsMsg{chains: chains, err: err}
	}
}

func loadSwapsCmd(client *SwapDaemonClient) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := swapCommandContext()
		defer cancel()
		swaps, err := client.ListSwaps(ctx)
		return swapListMsg{swaps: swaps, err: err}
	}
}

func loadSwapStatusCmd(client *SwapDaemonClient, swapID string) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := swapCommandContext()
		defer cancel()
		swap, err := client.SwapStatus(ctx, swapID)
		return swapStatusMsg{swap: swap, err: err}
	}
}

func initiateSwapCmd(client *SwapDaemonClient, request SwapInitiateRequest) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := swapCommandContext()
		defer cancel()
		result, err := client.Initiate(ctx, request)
		return swapMutationMsg{
			method:       "initiate_swap",
			swapID:       result.SwapID,
			ourPublicKey: result.OurSwapPubKey,
			err:          err,
		}
	}
}

func swapActionCmd(client *SwapDaemonClient, method, swapID string) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := swapCommandContext()
		defer cancel()
		var result SwapActionResult
		var err error
		if method == "accept" {
			result, err = client.Accept(ctx, swapID)
		} else {
			result, err = client.Refund(ctx, swapID)
		}
		return swapMutationMsg{method: method, swapID: result.SwapID, state: result.State, err: err}
	}
}

func (m model) handleSwapChains(msg swapChainsMsg) (tea.Model, tea.Cmd) {
	m.swapsUpdating = false
	if msg.err != nil {
		m.appendLog("Swap chain load failed: " + msg.err.Error())
		m.statusMsg = "Swap daemon unavailable or blocked"
		m.currentView = viewMenu
		return m, nil
	}
	m.swapChains = msg.chains
	keys := make([]string, 0, len(msg.chains))
	for _, chain := range msg.chains {
		keys = append(keys, chain.Key)
	}
	m.inputBuf = ""
	m.inputPrompt = "Ready native chains: " + strings.Join(keys, ", ") + "\nChain key:"
	m.currentView = viewCrossSwapChain
	m.statusMsg = fmt.Sprintf("%d protocol-ready chains", len(msg.chains))
	return m, nil
}

func (m model) handleSwapList(msg swapListMsg) (tea.Model, tea.Cmd) {
	m.swapsUpdating = false
	if msg.err != nil {
		m.appendLog("Swap list failed: " + msg.err.Error())
		m.statusMsg = "Swap list failed"
		m.currentView = viewMenu
		return m, nil
	}
	m.activeSwaps = msg.swaps
	m.currentView = viewActiveSwaps
	m.statusMsg = fmt.Sprintf("%d swaps loaded", len(msg.swaps))
	return m, nil
}

func (m model) handleSwapStatus(msg swapStatusMsg) (tea.Model, tea.Cmd) {
	m.swapsUpdating = false
	if msg.err != nil {
		m.appendLog("Swap status failed: " + msg.err.Error())
		m.statusMsg = "Swap status failed"
		m.currentView = viewActiveSwaps
		return m, nil
	}
	m.crossSwapDetail = msg.swap
	m.crossSwapID = stringValue(msg.swap, "swapId")
	m.currentView = viewCrossSwapDetail
	m.statusMsg = "Swap status loaded"
	return m, nil
}

func (m model) handleSwapMutation(msg swapMutationMsg) (tea.Model, tea.Cmd) {
	m.swapsUpdating = false
	if msg.err != nil {
		m.appendLog(msg.err.Error())
		m.statusMsg = msg.method + " failed"
		m.currentView = viewMenu
		return m, nil
	}
	if msg.method == "initiate_swap" {
		m.appendLog("Swap initiated: " + msg.swapID)
		m.appendLog("Our swap public key: " + msg.ourPublicKey)
		m.statusMsg = "Swap initiated: " + msg.swapID
	} else {
		m.appendLog(fmt.Sprintf("Swap %s succeeded: %s %s", msg.method, msg.swapID, msg.state))
		m.statusMsg = "Swap " + msg.method + " succeeded"
	}
	m.swapsUpdating = true
	return m, loadSwapsCmd(m.swapClient)
}

func (m model) handleCrossSwapInput(key string) (tea.Model, tea.Cmd) {
	switch m.currentView {
	case viewCrossSwapChain:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			for _, chain := range m.swapChains {
				if strings.EqualFold(chain.Key, value) {
					m.swapChain = chain
					m.inputBuf = ""
					m.inputPrompt = "Role: alice holds the counterparty asset; bob holds XFG (alice/bob):"
					m.currentView = viewCrossSwapRole
					return m, nil
				}
			}
			m.statusMsg = "Chain is not protocol-ready"
			return m, nil
		})
	case viewCrossSwapRole:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			value = strings.ToLower(value)
			if value != "alice" && value != "bob" {
				m.statusMsg = "Role must be alice or bob"
				return m, nil
			}
			m.crossSwapRole = value
			m.inputBuf = ""
			m.inputPrompt = "XFG amount (whole units, up to 7 decimals):"
			m.currentView = viewCrossSwapXFGAmount
			return m, nil
		})
	case viewCrossSwapXFGAmount:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			atomic, err := parseXFGAmount(value)
			if err != nil {
				m.statusMsg = err.Error()
				return m, nil
			}
			m.crossSwapXFG = atomic
			m.inputBuf = ""
			m.inputPrompt = fmt.Sprintf("%s native amount (whole units, up to %d decimals; tokens are not supported):",
				m.swapChain.AssetTicker, m.swapChain.Decimals)
			m.currentView = viewCrossSwapCounterpartyAmount
			return m, nil
		})
	case viewCrossSwapCounterpartyAmount:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			atomic, err := parseCounterpartyAmount(value, m.swapChain.Decimals)
			if err != nil {
				m.statusMsg = err.Error()
				return m, nil
			}
			m.crossSwapCtr = atomic
			m.inputBuf = ""
			m.inputPrompt = "Counterparty swap endpoint (host:port):"
			m.currentView = viewCrossSwapPeer
			return m, nil
		})
	case viewCrossSwapPeer:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			if err := validatePeerEndpoint(value); err != nil {
				m.statusMsg = err.Error()
				return m, nil
			}
			m.crossSwapPeer = value
			m.inputBuf = ""
			m.inputPrompt = "Expected counterparty Ed25519 swap public key (64 hex):"
			m.currentView = viewCrossSwapPeerKey
			return m, nil
		})
	case viewCrossSwapPeerKey:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			value = strings.ToLower(value)
			if err := validateExpectedPeerKey(value); err != nil {
				m.statusMsg = err.Error()
				return m, nil
			}
			m.crossSwapKey = value
			m.inputBuf = ""
			m.inputPrompt = fmt.Sprintf(
				"Review: %s XFG / %s %s on %s as %s\nPeer: %s\nType YES to initiate:",
				formatAtomicAmount(m.crossSwapXFG, xfgDecimals),
				formatAtomicAmount(m.crossSwapCtr, m.swapChain.Decimals), m.swapChain.AssetTicker,
				m.swapChain.Key, m.crossSwapRole, m.crossSwapPeer)
			m.currentView = viewCrossSwapConfirm
			return m, nil
		})
	case viewCrossSwapConfirm:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			m.inputBuf = ""
			if value != "YES" {
				m.statusMsg = "Swap initiation cancelled"
				m.currentView = viewMenu
				return m, nil
			}
			request := SwapInitiateRequest{
				Pair:               m.swapChain.Key,
				Role:               m.crossSwapRole,
				XFGAmount:          m.crossSwapXFG,
				CounterpartyAmount: m.crossSwapCtr,
				Peer:               m.crossSwapPeer,
				ExpectedPeerKey:    m.crossSwapKey,
			}
			m.currentView = viewMenu
			m.swapsUpdating = true
			m.statusMsg = "Initiating swap"
			return m, initiateSwapCmd(m.swapClient, request)
		})
	case viewCrossSwapActionID:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			if err := validateSwapID(value); err != nil {
				m.statusMsg = err.Error()
				return m, nil
			}
			m.crossSwapID = value
			m.inputBuf = ""
			if m.crossSwapAction == "inspect" {
				m.swapsUpdating = true
				return m, loadSwapStatusCmd(m.swapClient, value)
			}
			m.inputPrompt = fmt.Sprintf("Type YES to %s swap %s:", m.crossSwapAction, value)
			m.currentView = viewCrossSwapActionConfirm
			return m, nil
		})
	case viewCrossSwapActionConfirm:
		return m.editField(key, func(value string) (tea.Model, tea.Cmd) {
			m.inputBuf = ""
			if value != "YES" {
				m.statusMsg = "Swap action cancelled"
				m.currentView = viewActiveSwaps
				return m, nil
			}
			m.currentView = viewMenu
			m.swapsUpdating = true
			m.statusMsg = "Submitting swap " + m.crossSwapAction
			return m, swapActionCmd(m.swapClient, m.crossSwapAction, m.crossSwapID)
		})
	}
	return m, nil
}

func (m model) handleActiveSwapsKey(key string) (tea.Model, tea.Cmd) {
	if key == "esc" || key == "q" {
		m.currentView = viewMenu
		return m, nil
	}
	if key == "r" {
		m.swapsUpdating = true
		return m, loadSwapsCmd(m.swapClient)
	}
	actions := map[string]string{"s": "inspect", "a": "accept", "f": "refund"}
	action, ok := actions[key]
	if !ok {
		return m, nil
	}
	m.crossSwapAction = action
	m.inputBuf = ""
	m.inputPrompt = fmt.Sprintf("Swap ID to %s:", action)
	m.currentView = viewCrossSwapActionID
	return m, nil
}

func (m model) handleCrossSwapDetailKey(key string) (tea.Model, tea.Cmd) {
	if key == "esc" || key == "q" || key == "enter" {
		m.currentView = viewActiveSwaps
		return m, nil
	}
	if key == "r" && m.crossSwapID != "" {
		m.swapsUpdating = true
		return m, loadSwapStatusCmd(m.swapClient, m.crossSwapID)
	}
	return m, nil
}

func (m model) viewCrossSwapDetail() string {
	title := lipgloss.NewStyle().Foreground(bg).Background(accent).Bold(true).Padding(0, 2).Render("  SWAP STATUS  ")
	record := m.crossSwapDetail
	lines := []string{
		"ID: " + stringValue(record, "swapId"),
		"Pair: " + stringValue(record, "pairName"),
		"State: " + stringValue(record, "stateName"),
		"XFG: " + formatAtomicAmount(stringValue(record, "xfgAmountAtomic"), xfgDecimals),
		"Counterparty atomic: " + stringValue(record, "ctrAmount"),
		"Confirmations: " + exactJSONInteger(record["confirmations"]) + "/" + exactJSONInteger(record["requiredConfirmations"]),
		"Counterparty lock tx: " + stringValue(record, "ctrLockTxId"),
		"XFG claim tx: " + stringValue(record, "escrowClaimTxId"),
		"XFG refund tx: " + stringValue(record, "escrowRefundTxId"),
	}
	body := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(borderC).Padding(1, 2).Width(76).Render(strings.Join(lines, "\n"))
	hint := lipgloss.NewStyle().Foreground(muted).Render("R refresh | Esc return")
	return lipgloss.NewStyle().MarginTop(2).Render(lipgloss.JoinVertical(lipgloss.Center, title, body, hint))
}

func stringValue(values map[string]any, key string) string {
	value, ok := values[key]
	if !ok || value == nil {
		return "-"
	}
	if text, ok := value.(string); ok {
		if text == "" {
			return "-"
		}
		return text
	}
	return exactJSONInteger(value)
}

func exactJSONInteger(value any) string {
	switch number := value.(type) {
	case json.Number:
		return number.String()
	case string:
		return number
	case int:
		return strconv.Itoa(number)
	case int64:
		return strconv.FormatInt(number, 10)
	case uint64:
		return strconv.FormatUint(number, 10)
	case float64:
		return strconv.FormatFloat(number, 'f', -1, 64)
	default:
		return "-"
	}
}
