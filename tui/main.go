package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

var (
	accent    = lipgloss.Color("#FF6B35")
	accentDim = lipgloss.Color("#CC4400")
	bg        = lipgloss.Color("#0a0a0a")
	fg        = lipgloss.Color("#D0D0D0")
	muted     = lipgloss.Color("#555555")
	borderC   = lipgloss.Color("#2a2a2a")
	good      = lipgloss.Color("#00CC66")
	warn      = lipgloss.Color("#FFAA00")
	bad       = lipgloss.Color("#FF4444")

	base = lipgloss.NewStyle().Foreground(fg)
)

type versionInfo struct {
	projectName    string
	projectVersion string
	fullVersion    string
}

var verInfo = versionInfo{
	projectName:    "Fuego",
	projectVersion: "v1.10.0",
}

type menuItem string

const (
	mStartNode    menuItem = "Start Node"
	mStopNode     menuItem = "Stop Node"
	mNodeStatus   menuItem = "Node Status"
	mCreateWallet menuItem = "Create Wallet"
	mOpenWallet   menuItem = "Open Wallet"
	mGetBalance   menuItem = "Get Balance"
	mSendTx       menuItem = "Send Transaction"
	mBurn2Mint    menuItem = "Burn2Mint"
	mHeatMetrics  menuItem = "HEAT Metrics"
	mEternalFlame menuItem = "Eternal Flame"
	mHearthPool   menuItem = "Hearth Pool"
	mCreateCD     menuItem = "Create CD"
	mViewDeposit  menuItem = "View Deposit"
	mShowLogs     menuItem = "Show Logs"
	mQuit         menuItem = "Quit"
)

var menu = []menuItem{
	mStartNode, mStopNode, mNodeStatus,
	mCreateWallet, mOpenWallet, mGetBalance, mSendTx,
	mBurn2Mint, mHeatMetrics, mEternalFlame, mHearthPool,
	mCreateCD, mViewDeposit,
	mShowLogs, mQuit,
}

type view int

const (
	viewMenu view = iota
	viewSendTxAddr
	viewSendTxAmt
	viewBurn2MintChoose
	viewCreateCDAmt
	viewCreateCDTerm
	viewViewDeposit
	viewLogs
	viewNodeInfo
	viewWalletInfo
	viewDaemonData
	viewWalletFile
)

type logMsg struct{ line string }

type nodePollMsg struct {
	info nodeInfo
	err  error
}

type rpcResultMsg struct {
	method string
	result map[string]interface{}
	err    error
}

type daemonResultMsg struct {
	path   string
	result map[string]interface{}
	err    error
}

type pipeDataMsg struct {
	prefix string
	data   string
}

type pipeDoneMsg struct {
	prefix string
	err    error
}

type tickMsg struct{}

type model struct {
	cursor       int
	nodeCmd      *exec.Cmd
	walletCmd    *exec.Cmd
	logs         []string
	runningNode  bool
	runningW     bool
	height       int
	peers        int
	statusMsg    string
	currentView  view
	inputBuf     string
	inputPrompt  string
	txAddr       string
	txAmt        string
	cdAmt        string
	cdTerm       string
	depositId    string

	nodeHeight    int
	nodePeers     int
	nodeDifficulty int64
	nodeTxCount   int
	nodeTxPool    int
	nodeIncoming  int
	nodeOutgoing  int
	nodeVersion   string
	nodeTopHash   string
	nodeExternal  bool
	nodeUpdating  bool
	nodeShowInfo  bool
	nodeError     string

	daemonData map[string]interface{}
	daemonPath string

	nodeStdout   io.Reader
	nodeStderr   io.Reader
	walletStdout io.Reader
	walletStderr io.Reader

	walletPort    int
	containerFile string
	containerPass string
	walletAddress string
	balanceXfg    int64
	balanceHeat   int64
}

func (m *model) appendLog(s string) {
	m.logs = append(m.logs, fmt.Sprintf("%s %s", time.Now().Format("15:04:05"), s))
	if len(m.logs) > 200 {
		m.logs = m.logs[len(m.logs)-200:]
	}
}

func initialModel() model {
	modeStr := "MAINNET"
	if CurrentConfig.IsTestnet {
		modeStr = "TESTNET"
	}
	verInfo.fullVersion = fmt.Sprintf("%s %s || %s", verInfo.projectName, modeStr, verInfo.projectVersion)

	cFile := filepath.Join(dataDir(), "wallet.container")
	m := model{
		cursor:       0,
		logs: []string{
			fmt.Sprintf("%s TUI Ready (%s Mode)", verInfo.projectName, modeStr),
			fmt.Sprintf("Node: %s (RPC %d) | Wallet RPC: %d", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort, CurrentConfig.NodeRPCPort+3),
		},
		statusMsg:     "",
		walletPort:    CurrentConfig.NodeRPCPort + 3,
		containerFile: cFile,
		containerPass: loadPassword(cFile),
	}
	return m
}

func dataDir() string {
	if runtime.GOOS == "darwin" {
		base := "Library/Application Support/Fuego"
		if CurrentConfig.IsTestnet {
			base = filepath.Join(base, "testnet")
		}
		return filepath.Join(os.Getenv("HOME"), base)
	}
	return filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
}

func (m model) Init() tea.Cmd {
	return probeNodeCmd()
}

func probeNodeCmd() tea.Cmd {
	return func() tea.Msg {
		time.Sleep(500 * time.Millisecond)
		return pollNodeCmd()()
	}
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m.handleKey(msg)
	case logMsg:
		m.appendLog(msg.line)
		return m, nil
	case nodePollMsg:
		if msg.err != nil {
			if m.nodeCmd != nil {
				m.statusMsg = "Node offline"
			}
		} else {
			m.runningNode = true
			m.populateNodeInfo(msg.info)
			if m.nodeShowInfo {
				m.nodeShowInfo = false
				m.currentView = viewNodeInfo
			}
		}
		return m, nil
	case rpcResultMsg:
		return m.handleRpcResult(msg)
	case daemonResultMsg:
		return m.handleDaemonResult(msg)
	case pipeDataMsg:
		m.appendLog(fmt.Sprintf("%s: %s", msg.prefix, strings.TrimSpace(msg.data)))
		return m, reschedulePipe(&m, msg.prefix)
	case pipeDoneMsg:
		if msg.err != nil && msg.err != io.EOF {
			m.appendLog(fmt.Sprintf("%s pipe: %v", msg.prefix, msg.err))
		}
		switch msg.prefix {
		case "NODE":
			m.nodeStdout = nil
		case "NODE-ERR":
			m.nodeStderr = nil
		case "WALLET":
			m.walletStdout = nil
		case "WALLET-ERR":
			m.walletStderr = nil
		}
		return m, nil
	case tickMsg:
		var cmds []tea.Cmd
		cmds = append(cmds, pollNodeCmd())
		if m.runningW {
			cmds = append(cmds, walletRpcCmd(m.walletPort, "getBalance", map[string]interface{}{}))
		}
		cmds = append(cmds, pollNodeTick())
		return m, tea.Batch(cmds...)
	}
	return m, nil
}

func (m model) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	k := msg.String()
	if m.currentView != viewMenu {
		return m.handleSubViewKey(k)
	}
	switch k {
	case "up", "k":
		if m.cursor > 0 {
			m.cursor--
		}
	case "down", "j":
		if m.cursor < len(menu)-1 {
			m.cursor++
		}
	case "enter":
		return m.executeMenuItem()
	case "q", "ctrl+c":
		return m, tea.Quit
	}
	return m, nil
}

func (m model) handleSubViewKey(k string) (tea.Model, tea.Cmd) {
	switch m.currentView {
	case viewSendTxAddr:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.txAddr = newVal
			m.inputBuf = ""
			m.inputPrompt = "Amount (XFG): "
			m.currentView = viewSendTxAmt
			return m, nil
		})
	case viewSendTxAmt:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.txAmt = newVal
			m.inputBuf = ""
			m.currentView = viewMenu
			return m.sendTx()
		})
	case viewBurn2MintChoose:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.inputBuf = ""
			m.currentView = viewMenu
			return m.executeBurn(newVal)
		})
	case viewCreateCDAmt:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.cdAmt = newVal
			m.inputBuf = ""
			m.inputPrompt = "Term (epochs, 0xFFFFFFFF=HEAT): "
			m.currentView = viewCreateCDTerm
			return m, nil
		})
	case viewCreateCDTerm:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.cdTerm = newVal
			m.inputBuf = ""
			m.currentView = viewMenu
			return m.createCD()
		})
	case viewViewDeposit:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.depositId = newVal
			m.inputBuf = ""
			m.currentView = viewMenu
			return m.viewDeposit()
		})
	case viewLogs:
		if k == "esc" || k == "enter" || k == "q" {
			m.currentView = viewMenu
			return m, nil
		}
	case viewNodeInfo:
		if k == "esc" || k == "enter" || k == "q" {
			m.currentView = viewMenu
			return m, nil
		}
		if k == "r" {
			m.nodeUpdating = true
			return m, pollNodeCmd()
		}
	case viewWalletInfo:
		if k == "esc" || k == "enter" || k == "q" {
			m.currentView = viewMenu
			return m, nil
		}
		if k == "r" {
			return m, walletRpcCmd(m.walletPort, "getBalance", map[string]interface{}{})
		}
	case viewDaemonData:
		if k == "esc" || k == "enter" || k == "q" {
			m.currentView = viewMenu
			return m, nil
		}
	case viewWalletFile:
		return m.editField(k, func(newVal string) (tea.Model, tea.Cmd) {
			m.inputBuf = ""
			m.currentView = viewMenu
			if strings.TrimSpace(newVal) != "" {
				m.containerFile = strings.TrimSpace(newVal)
			}
			return m.openWallet()
		})
	}
	return m, nil
}

type fieldCallback func(string) (tea.Model, tea.Cmd)

func (m model) editField(k string, onEnter fieldCallback) (tea.Model, tea.Cmd) {
	if k == "esc" {
		m.currentView = viewMenu
		m.inputBuf = ""
		return m, nil
	}
	if k == "enter" {
		return onEnter(strings.TrimSpace(m.inputBuf))
	}
	if k == "backspace" {
		if len(m.inputBuf) > 0 {
			m.inputBuf = m.inputBuf[:len(m.inputBuf)-1]
		}
		return m, nil
	}
	if len(k) == 1 {
		m.inputBuf += k
	}
	return m, nil
}

func (m model) executeMenuItem() (tea.Model, tea.Cmd) {
	item := menu[m.cursor]
	switch item {
	case mStartNode:
		return m.startNode()
	case mStopNode:
		return m.stopNode()
	case mNodeStatus:
		return m.nodeStatus()
	case mCreateWallet:
		return m.createWallet()
	case mOpenWallet:
		if m.runningW {
			m.appendLog("Wallet RPC already running")
			m.statusMsg = "Wallet already running"
			return m, nil
		}
		m.currentView = viewWalletFile
		m.inputBuf = m.containerFile
		m.inputPrompt = "Container file path: "
		return m, nil
	case mGetBalance:
		return m.getBalance()
	case mSendTx:
		m.currentView = viewSendTxAddr
		m.inputBuf = ""
		m.inputPrompt = "Recipient address: "
		return m, nil
	case mBurn2Mint:
		m.currentView = viewBurn2MintChoose
		m.inputBuf = ""
		m.inputPrompt = fmt.Sprintf("Burn amount: 0) %.2f XFG  1) %.2f XFG  2) %.2f XFG  3) %.2f XFG (enter 0-3): ",
			float64(CurrentConfig.BurnTiers[0])/float64(CurrentConfig.CoinUnits),
			float64(CurrentConfig.BurnTiers[1])/float64(CurrentConfig.CoinUnits),
			float64(CurrentConfig.BurnTiers[2])/float64(CurrentConfig.CoinUnits),
			float64(CurrentConfig.BurnTiers[3])/float64(CurrentConfig.CoinUnits))
		return m, nil
	case mHeatMetrics:
		m.daemonPath = "/heat_metrics"
		m.currentView = viewDaemonData
		return m, daemonGetCmd("/heat_metrics")
	case mEternalFlame:
		m.daemonPath = "/getethereal"
		m.currentView = viewDaemonData
		return m, daemonGetCmd("/getethereal")
	case mHearthPool:
		m.daemonPath = "/amm_pool_info"
		m.currentView = viewDaemonData
		return m, daemonGetCmd("/amm_pool_info")
	case mCreateCD:
		m.currentView = viewCreateCDAmt
		m.inputBuf = ""
		m.inputPrompt = "CD amount (XFG): "
		return m, nil
	case mViewDeposit:
		m.currentView = viewViewDeposit
		m.inputBuf = ""
		m.inputPrompt = "Deposit ID: "
		return m, nil
	case mShowLogs:
		m.currentView = viewLogs
		return m, nil
	case mQuit:
		m.shutdown()
		return m, tea.Quit
	}
	return m, nil
}

func (m model) handleRpcResult(msg rpcResultMsg) (tea.Model, tea.Cmd) {
	if msg.err != nil {
		m.appendLog(fmt.Sprintf("%s failed: %v", msg.method, msg.err))
		m.statusMsg = msg.method + " failed"
		return m, nil
	}

	result := msg.result
	switch msg.method {
	case "getBalance":
		bal := valueInt(result, "availableBalance")
		m.balanceXfg = bal
		heatLocked := valueInt(result, "lockedHeatBalance")
		heatUnlocked := valueInt(result, "unlockedHeatBalance")
		heatTotal := heatLocked + heatUnlocked
		m.balanceHeat = heatUnlocked
		m.appendLog(fmt.Sprintf("Balance: %.2f XFG | H\u2CB6\u2206T: %d (locked:%d unlocked:%d)",
			float64(bal)/float64(CurrentConfig.CoinUnits), heatTotal, heatLocked, heatUnlocked))
		m.statusMsg = fmt.Sprintf("Balance: %.2f XFG", float64(bal)/float64(CurrentConfig.CoinUnits))

	case "createAddress":
		if addr, ok := result["address"].(string); ok {
			m.walletAddress = addr
			m.appendLog("Wallet address: " + addr)
			m.statusMsg = "Wallet created: " + truncate(addr, 12)
		}

	case "sendTransaction":
		if txh, ok := result["transactionHash"].(string); ok {
			m.appendLog("Transaction sent: " + txh)
			m.statusMsg = "Tx sent: " + truncate(txh, 12)
		}

	case "createBurnDeposit":
		if txh, ok := result["transactionHash"].(string); ok {
			heatAmt := valueInt(result, "heatAmount")
			m.appendLog(fmt.Sprintf("Burn tx: %s | HEAT minted: %d", txh, heatAmt))
			m.statusMsg = "Burn successful: " + truncate(txh, 12)
		}

	case "createDeposit":
		if txh, ok := result["transactionHash"].(string); ok {
			isBurn := false
			if v, ok := result["isBurnDeposit"].(bool); ok {
				isBurn = v
			}
			depType := "YIELD CD"
			if isBurn {
				depType = "HEAT CD"
			}
			m.appendLog(fmt.Sprintf("%s created: %s", depType, txh))
			m.statusMsg = depType + " created"
		}

	case "withdrawDeposit":
		if txh, ok := result["transactionHash"].(string); ok {
			m.appendLog("Deposit withdrawn: " + txh)
			m.statusMsg = "Deposit withdrawn"
		}

	case "getDeposit":
		amt := valueInt(result, "amount")
		term := valueInt(result, "term")
		interest := valueInt(result, "interest")
		locked := false
		if v, ok := result["locked"].(bool); ok {
			locked = v
		}
		height := valueInt(result, "height")
		unlockH := valueInt(result, "unlockHeight")
		m.appendLog(fmt.Sprintf("Deposit: %.2f XFG | Term: %d epochs | Interest: %.2f | Height: %d | Unlock: %d | Locked: %v",
			float64(amt)/float64(CurrentConfig.CoinUnits), term,
			float64(interest)/float64(CurrentConfig.CoinUnits), height, unlockH, locked))
		m.statusMsg = "Deposit details loaded"

	case "getAddresses":
		if addrs, ok := result["addresses"].([]interface{}); ok && len(addrs) > 0 {
			if addr, ok := addrs[0].(string); ok {
				m.walletAddress = addr
				m.appendLog("Wallet address: " + addr)
			}
		}
		if m.walletAddress == "" {
			m.appendLog("No addresses found, creating one...")
			return m, walletRpcCmd(m.walletPort, "createAddress", map[string]interface{}{})
		}
		m.statusMsg = "Wallet open"

	case "getStatus":
		if bc, ok := result["blockCount"].(float64); ok {
			m.appendLog(fmt.Sprintf("Wallet synced — blocks: %.0f", bc))
		}
	}
	return m, nil
}

func (m model) handleDaemonResult(msg daemonResultMsg) (tea.Model, tea.Cmd) {
	if msg.err != nil {
		m.appendLog(fmt.Sprintf("daemon %s failed: %v", msg.path, msg.err))
		m.statusMsg = "Daemon query failed"
		return m, nil
	}

	m.daemonData = msg.result
	m.daemonPath = msg.path
	m.currentView = viewDaemonData
	return m, nil
}

func (m model) createCD() (tea.Model, tea.Cmd) {
	if !m.runningW {
		m.appendLog("Wallet RPC not running")
		m.statusMsg = "Wallet offline"
		return m, nil
	}
	if m.walletAddress == "" {
		m.appendLog("No wallet address — create wallet first")
		m.statusMsg = "No wallet"
		return m, nil
	}

	amtFloat, err := strconv.ParseFloat(m.cdAmt, 64)
	if err != nil || amtFloat <= 0 {
		m.appendLog("Invalid CD amount: " + m.cdAmt)
		m.statusMsg = "Invalid amount"
		return m, nil
	}
	amountAtomic := int64(amtFloat * float64(CurrentConfig.CoinUnits))

	termStr := strings.TrimSpace(m.cdTerm)
	var term uint64
	if strings.Contains(strings.ToUpper(termStr), "HEAT") || termStr == "0xFFFFFFFF" || termStr == "4294967295" {
		term = 0xFFFFFFFF
	} else {
		t, err := strconv.ParseUint(termStr, 10, 64)
		if err != nil {
			m.appendLog("Invalid term: " + termStr)
			m.statusMsg = "Invalid term"
			return m, nil
		}
		term = t
	}

	depType := "YIELD CD"
	if term == 0xFFFFFFFF {
		depType = "HEAT CD"
	}

	m.appendLog(fmt.Sprintf("Creating %s: %.2f XFG, term=%d...", depType, amtFloat, term))
	params := map[string]interface{}{
		"amount":        amountAtomic,
		"term":          term,
		"sourceAddress": m.walletAddress,
	}
	return m, walletRpcCmd(m.walletPort, "createDeposit", params)
}

func (m model) viewDeposit() (tea.Model, tea.Cmd) {
	if !m.runningW {
		m.appendLog("Wallet RPC not running")
		m.statusMsg = "Wallet offline"
		return m, nil
	}

	id, err := strconv.Atoi(strings.TrimSpace(m.depositId))
	if err != nil {
		m.appendLog("Invalid deposit ID: " + m.depositId)
		m.statusMsg = "Invalid ID"
		return m, nil
	}

	m.appendLog(fmt.Sprintf("Fetching deposit %d...", id))
	return m, walletRpcCmd(m.walletPort, "getDeposit", map[string]interface{}{"depositId": id})
}

func (m model) executeBurn(choice string) (tea.Model, tea.Cmd) {
	if !m.runningW {
		m.appendLog("Wallet RPC not running")
		m.statusMsg = "Wallet offline"
		return m, nil
	}
	if m.walletAddress == "" {
		m.appendLog("No wallet address")
		m.statusMsg = "No wallet"
		return m, nil
	}

	idx, err := strconv.Atoi(choice)
	if err != nil || idx < 0 || idx >= len(CurrentConfig.BurnTiers) {
		m.appendLog("Invalid burn tier: " + choice)
		m.statusMsg = "Invalid tier"
		return m, nil
	}

	amount := CurrentConfig.BurnTiers[idx]
	m.appendLog(fmt.Sprintf("Burning %.2f XFG → HEAT via createBurnDeposit...",
		float64(amount)/float64(CurrentConfig.CoinUnits)))
	params := map[string]interface{}{
		"amount":        amount,
		"sourceAddress": m.walletAddress,
	}
	return m, walletRpcCmd(m.walletPort, "createBurnDeposit", params)
}

func passFile(containerFile string) string { return containerFile + ".pass" }

func loadPassword(containerFile string) string {
	b, err := os.ReadFile(passFile(containerFile))
	if err == nil {
		return strings.TrimSpace(string(b))
	}
	return ""
}

func savePassword(containerFile, pass string) {
	os.WriteFile(passFile(containerFile), []byte(pass), 0600)
}

func (m model) shutdown() {
	if m.nodeCmd != nil {
		_ = m.nodeCmd.Process.Kill()
		m.appendLog("Node stopped")
	}
	if m.walletCmd != nil {
		_ = m.walletCmd.Process.Kill()
		m.appendLog("Wallet RPC stopped")
	}
}

// ── View ────────────────────────────────────────────────────────────────────

func (m model) View() string {
	switch m.currentView {
	case viewLogs:
		return m.viewLogs()
	case viewSendTxAddr, viewSendTxAmt, viewBurn2MintChoose,
		viewCreateCDAmt, viewCreateCDTerm, viewViewDeposit, viewWalletFile:
		return m.viewInput()
	case viewNodeInfo:
		return m.viewNodeDetails()
	case viewWalletInfo:
		return m.viewWalletDetails()
	case viewDaemonData:
		return m.viewDaemonDetails()
	default:
		return m.viewMenu()
	}
}

func (m model) viewMenu() string {
	titleText := "  FUEGO TUI  "
	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		MarginBottom(1).
		Render(titleText)

	version := lipgloss.NewStyle().Foreground(muted).Render(verInfo.fullVersion)

	menuItems := ""
	for i, it := range menu {
		cursor := "  "
		line := lipgloss.NewStyle().Foreground(muted)
		if m.cursor == i {
			cursor = lipgloss.NewStyle().Foreground(accent).Bold(true).Render("▸ ")
			line = lipgloss.NewStyle().Foreground(fg).Bold(true)
		}
		menuItems += cursor + line.Render(string(it)) + "\n"
	}

	menuBox := lipgloss.NewStyle().
		Align(lipgloss.Center).
		Padding(1, 3).
		Render(menuItems)

	boxed := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(borderC).
		Padding(0, 2).
		MarginTop(1).MarginBottom(1).
		Width(38).Align(lipgloss.Center).
		Render(menuBox)

	// Status bar — single clean line
	nodeDot := lipgloss.NewStyle().Foreground(muted).Render("\u25CB")
	nodeLabel := fmt.Sprintf("Node:offline")
	if m.runningNode {
		nodeDot = lipgloss.NewStyle().Foreground(good).Render("\u25CF")
		nodeLabel = fmt.Sprintf("Node:synced")
	}
	nodePart := nodeDot + " " + nodeLabel

	heightPart := ""
	if m.runningNode {
		heightPart = lipgloss.NewStyle().Foreground(muted).Render(
			fmt.Sprintf(" \u00B7 H:%d", m.nodeHeight))
	}

	walletPart := ""
	if m.runningW && m.walletAddress != "" {
		wAddr := m.walletAddress
		if len(wAddr) > 9 {
			wAddr = wAddr[:9]
		}
		walletPart = lipgloss.NewStyle().Foreground(muted).Render(
			fmt.Sprintf(" \u00B7 W:%s", wAddr))
	}

	balancePart := ""
	if m.runningW {
		xfgBal := float64(m.balanceXfg) / float64(CurrentConfig.CoinUnits)
		heatBal := float64(m.balanceHeat) / float64(CurrentConfig.CoinUnits)
		balancePart = lipgloss.NewStyle().Foreground(muted).Render(
			fmt.Sprintf(" \u00B7 \u20B2%.2f || H\u2CB6\u2206T %.2f", xfgBal, heatBal))
	}

	bottom := lipgloss.NewStyle().Foreground(fg).Width(60).Align(lipgloss.Center).Render(
		nodePart + heightPart + walletPart + balancePart)

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		version,
		boxed,
		bottom,
	)

	return lipgloss.NewStyle().MarginTop(1).Render(content)
}

func (m model) viewInput() string {
	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		Render(" " + m.inputPrompt[:min(len(m.inputPrompt), 40)] + " ")

	prompt := lipgloss.NewStyle().Foreground(muted).Render(m.inputPrompt)
	cursor := lipgloss.NewStyle().Foreground(accent).Blink(true).Render("▌")
	input := lipgloss.NewStyle().Foreground(fg).Render(m.inputBuf)

	hint := lipgloss.NewStyle().Foreground(muted).Render("Enter = confirm  ·  Esc = cancel")

	boxed := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(accentDim).
		Padding(1, 2).
		Width(52).Align(lipgloss.Center).
		Render(prompt + "\n\n" + input + cursor + "\n")

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		boxed,
		hint,
	)

	return lipgloss.NewStyle().MarginTop(3).Render(content)
}

func (m model) viewLogs() string {
	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		Render("  LOGS  ")

	start := len(m.logs) - 25
	if start < 0 {
		start = 0
	}
	logText := ""
	for _, l := range m.logs[start:] {
		logText += lipgloss.NewStyle().Foreground(muted).Render(l) + "\n"
	}

	hint := lipgloss.NewStyle().Foreground(muted).Render("Esc/Enter/Q to return")

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		logText,
		hint,
	)

	return lipgloss.NewStyle().MarginTop(1).Render(content)
}

func (m model) viewNodeDetails() string {
	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		Render("  NODE DETAILS  ")

	if m.nodeError != "" {
		errStyle := lipgloss.NewStyle().Foreground(bad)
		hint := lipgloss.NewStyle().Foreground(muted).Render("Esc/Enter/Q to return  ·  Try --node-port <port>")
		boxed := lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(borderC).
			Padding(1, 2).
			Width(52).
			Render(errStyle.Render(m.nodeError))
		content := lipgloss.JoinVertical(lipgloss.Center, title, boxed, hint)
		return lipgloss.NewStyle().MarginTop(2).Render(content)
	}

	nodeType := "LOCAL"
	if m.nodeExternal {
		nodeType = "EXTERNAL"
	}

	mutedStyle := lipgloss.NewStyle().Foreground(muted)
	valueStyle := lipgloss.NewStyle().Foreground(fg)
	accentV := lipgloss.NewStyle().Foreground(accent)

	if m.nodeUpdating {
		nodeType = accentV.Render("...refreshing...")
	}

	rows := [][2]string{
		{"Type", accentV.Render(nodeType)},
		{"Status", accentV.Render("connected")},
		{"Height", valueStyle.Render(fmt.Sprintf("%d", m.nodeHeight))},
		{"Peers", valueStyle.Render(fmt.Sprintf("%d (in:%d out:%d)", m.nodePeers, m.nodeIncoming, m.nodeOutgoing))},
		{"Difficulty", valueStyle.Render(fmt.Sprintf("%d", m.nodeDifficulty))},
		{"Txs", valueStyle.Render(fmt.Sprintf("%d", m.nodeTxCount))},
		{"Pool", valueStyle.Render(fmt.Sprintf("%d", m.nodeTxPool))},
		{"Version", valueStyle.Render(m.nodeVersion)},
		{"Top Hash", valueStyle.Render(m.nodeTopHash[:min(len(m.nodeTopHash), 16)]+"...")},
	}

	var sb strings.Builder
	for _, row := range rows {
		sb.WriteString(mutedStyle.Render(fmt.Sprintf("  %-14s", row[0])))
		sb.WriteString(row[1])
		sb.WriteString("\n")
	}

	hint := lipgloss.NewStyle().Foreground(muted).Render("Esc/Enter/Q to return  ·  R to refresh")

	boxed := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(borderC).
		Padding(1, 2).
		Width(52).
		Render(sb.String())

	content := lipgloss.JoinVertical(
		lipgloss.Center,
		title,
		boxed,
		hint,
	)

	return lipgloss.NewStyle().MarginTop(2).Render(content)
}

func (m model) viewWalletDetails() string {
	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		Render("  WALLET  ")

	if !m.runningW {
		hint := lipgloss.NewStyle().Foreground(muted).Render("Esc to return")
		msg := lipgloss.NewStyle().Foreground(muted).Render("Wallet RPC not running.")
		boxed := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(borderC).Padding(1, 2).Width(52).Render(msg)
		content := lipgloss.JoinVertical(lipgloss.Center, title, boxed, hint)
		return lipgloss.NewStyle().MarginTop(2).Render(content)
	}

	mutedStyle := lipgloss.NewStyle().Foreground(muted)
	valueStyle := lipgloss.NewStyle().Foreground(fg)
	accentV := lipgloss.NewStyle().Foreground(accent)

	status := accentV.Render("connected")
	addr := valueStyle.Render(truncate(m.walletAddress, 40))
	if m.walletAddress == "" {
		addr = mutedStyle.Render("fetching...")
	}

	xfgStr := mutedStyle.Render("fetching...")
	if m.balanceXfg > 0 || m.runningW {
		xfgStr = valueStyle.Render(fmt.Sprintf("\u20B2 %.2f", float64(m.balanceXfg)/float64(CurrentConfig.CoinUnits)))
	}

	heatStr := valueStyle.Render(fmt.Sprintf("H\u2CB6\u2206T %.2f", float64(m.balanceHeat)/float64(CurrentConfig.CoinUnits)))

	rows := [][2]string{
		{"Status", status},
		{"Address", addr},
		{"XFG Balance", xfgStr},
		{"HEAT Balance", heatStr},
		{"Container", valueStyle.Render(filepath.Base(m.containerFile))},
	}

	var sb strings.Builder
	for _, row := range rows {
		sb.WriteString(mutedStyle.Render(fmt.Sprintf("  %-14s", row[0])))
		sb.WriteString(row[1])
		sb.WriteString("\n")
	}

	hint := lipgloss.NewStyle().Foreground(muted).Render("Esc to return  ·  R to refresh balance")

	boxed := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(borderC).Padding(1, 2).Width(52).Render(sb.String())
	content := lipgloss.JoinVertical(lipgloss.Center, title, boxed, hint)
	return lipgloss.NewStyle().MarginTop(2).Render(content)
}

func (m model) viewDaemonDetails() string {
	titleText := "  DAEMON DATA  "
	switch m.daemonPath {
	case "/heat_metrics":
		titleText = "  HEAT METRICS  "
	case "/getethereal":
		titleText = "  ETERNAL FLAME  "
	case "/amm_pool_info":
		titleText = "  HEARTH AMM  "
	}

	title := lipgloss.NewStyle().
		Foreground(bg).Background(accent).Bold(true).Padding(0, 2).
		Render(titleText)

	if m.daemonData == nil {
		loading := lipgloss.NewStyle().Foreground(muted).Render("Loading...")
		boxed := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(borderC).Padding(1, 2).Width(52).Render(loading)
		hint := lipgloss.NewStyle().Foreground(muted).Render("Esc to return")
		content := lipgloss.JoinVertical(lipgloss.Center, title, boxed, hint)
		return lipgloss.NewStyle().MarginTop(2).Render(content)
	}

	mutedStyle := lipgloss.NewStyle().Foreground(muted)
	valueStyle := lipgloss.NewStyle().Foreground(fg)

	var rows [][2]string
	switch m.daemonPath {
	case "/heat_metrics":
		rows = [][2]string{
			{"Status", valueStyle.Render(fmt.Sprintf("%v", m.daemonData["status"]))},
			{"HEAT Supply", valueStyle.Render(fmt.Sprintf("%d", valueInt(m.daemonData, "heat_supply")))},
			{"Burned XFG", valueStyle.Render(fmt.Sprintf("%.2f", float64(valueInt(m.daemonData, "burned_xfg"))/float64(CurrentConfig.CoinUnits)))},
			{"Redemp. Price", valueStyle.Render(fmt.Sprintf("%d/%d", valueInt(m.daemonData, "redemption_price_num"), valueInt(m.daemonData, "redemption_price_denom")))},
			{"Redemp. Rate", valueStyle.Render(fmt.Sprintf("%d/%d", valueInt(m.daemonData, "redemption_rate_num"), valueInt(m.daemonData, "redemption_rate_denom")))},
			{"Treasury", valueStyle.Render(fmt.Sprintf("%.2f XFG", float64(valueInt(m.daemonData, "treasury_balance"))/float64(CurrentConfig.CoinUnits)))},
		}
	case "/getethereal":
		rows = [][2]string{
			{"Burned XFG", valueStyle.Render(fmt.Sprintf("%.2f", float64(valueInt(m.daemonData, "ethereal_xfg"))/float64(CurrentConfig.CoinUnits)))},
			{"Formatted", valueStyle.Render(fmt.Sprintf("%v", m.daemonData["formattedAmount"]))},
		}
	case "/amm_pool_info":
		rows = [][2]string{
			{"XFG Reserve", valueStyle.Render(fmt.Sprintf("%.2f", float64(valueInt(m.daemonData, "reserve_xfg"))/float64(CurrentConfig.CoinUnits)))},
			{"HEAT Reserve", valueStyle.Render(fmt.Sprintf("%d", valueInt(m.daemonData, "reserve_heat")))},
			{"LP Shares", valueStyle.Render(fmt.Sprintf("%d", valueInt(m.daemonData, "total_lp_shares")))},
			{"Spot Price", valueStyle.Render(fmt.Sprintf("%d", valueInt(m.daemonData, "spot_price")))},
			{"LP Fees", valueStyle.Render(fmt.Sprintf("%.2f XFG", float64(valueInt(m.daemonData, "accumulated_lp_fees"))/float64(CurrentConfig.CoinUnits)))},
			{"Epoch Fees", valueStyle.Render(fmt.Sprintf("%.2f XFG", float64(valueInt(m.daemonData, "epoch_swap_fees"))/float64(CurrentConfig.CoinUnits)))},
		}
	default:
		for k, v := range m.daemonData {
			rows = append(rows, [2]string{k, valueStyle.Render(fmt.Sprintf("%v", v))})
		}
	}

	var sb strings.Builder
	for _, row := range rows {
		sb.WriteString(mutedStyle.Render(fmt.Sprintf("  %-16s", row[0])))
		sb.WriteString(row[1])
		sb.WriteString("\n")
	}

	hint := lipgloss.NewStyle().Foreground(muted).Render("Esc to return")
	boxed := lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(borderC).Padding(1, 2).Width(52).Render(sb.String())
	content := lipgloss.JoinVertical(lipgloss.Center, title, boxed, hint)
	return lipgloss.NewStyle().MarginTop(2).Render(content)
}

// ── Binary & Process Management ─────────────────────────────────────────────

func binPath(name string) string {
	cwd, _ := os.Getwd()
	if filepath.Base(cwd) == "tui" {
		cwd = filepath.Dir(cwd)
	}
	search := []string{
		filepath.Join(cwd, "build", "src", name),
		filepath.Join(cwd, "build", "release", "src", name),
		filepath.Join(cwd, "..", "build", "src", name),
		filepath.Join(cwd, "..", "build", "release", "src", name),
	}
	for _, p := range search {
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return name
}

func (m model) startNode() (tea.Model, tea.Cmd) {
	if m.runningNode {
		m.appendLog("Node already running")
		m.statusMsg = "Node already running"
		return m, nil
	}

	info, err := getInfo(CurrentConfig.NodeRPCPort)
	if err == nil {
		m.runningNode = true
		m.nodeExternal = true
		m.nodeError = ""
		m.nodeUpdating = false
		m.populateNodeInfo(info)
		m.statusMsg = "External node connected"
		m.currentView = viewNodeInfo
		return m, pollNodeTick()
	}

	path := binPath(CurrentConfig.NodeBinary)
	if path == CurrentConfig.NodeBinary {
		if _, err := exec.LookPath(CurrentConfig.NodeBinary); err != nil {
			m.nodeError = fmt.Sprintf("No node on :%d and %s not found in PATH or build/", CurrentConfig.NodeRPCPort, CurrentConfig.NodeBinary)
			m.currentView = viewNodeInfo
			return m, nil
		}
	}

	dd := dataDir()
	os.MkdirAll(dd, 0755)

	args := []string{
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dd),
	}
	if CurrentConfig.IsTestnet {
		args = append(args, "--testnet")
	}

	cmd := exec.Command(path, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		m.appendLog("Failed to start node: " + err.Error())
		m.statusMsg = "Failed to start node"
		return m, nil
	}
	m.nodeCmd = cmd
	m.runningNode = true
	m.nodeStdout = stdout
	m.nodeStderr = stderr
	m.appendLog(fmt.Sprintf("Started %s on port %d", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort))
	m.statusMsg = "Node starting..."
	m.nodeShowInfo = true

	time.Sleep(1 * time.Second)
	return m, tea.Batch(
		streamPipe(stdout, "NODE"),
		streamPipe(stderr, "NODE-ERR"),
		pollNodeTick(),
	)
}

func (m model) stopNode() (tea.Model, tea.Cmd) {
	if !m.runningNode {
		m.appendLog("Node not running")
		m.statusMsg = "Node not running"
		return m, nil
	}
	if m.nodeCmd != nil {
		_ = m.nodeCmd.Process.Kill()
		m.appendLog("Node stopped")
	}
	m.nodeCmd = nil
	m.runningNode = false
	m.statusMsg = "Node disconnected"
	m.nodeHeight = 0
	m.nodePeers = 0
	m.nodeDifficulty = 0
	m.nodeTxCount = 0
	m.nodeTxPool = 0
	m.nodeIncoming = 0
	m.nodeOutgoing = 0
	m.nodeVersion = ""
	m.nodeTopHash = ""
	m.nodeExternal = false
	m.nodeError = ""
	return m, nil
}

func (m *model) populateNodeInfo(info nodeInfo) {
	m.nodeHeight = info.Height
	m.nodePeers = info.Peers
	m.nodeDifficulty = info.Difficulty
	m.nodeTxCount = info.TxCount
	m.nodeTxPool = info.TxPool
	m.nodeIncoming = info.Incoming
	m.nodeOutgoing = info.Outgoing
	m.nodeVersion = info.Version
	m.nodeTopHash = info.TopHash
}

func (m model) nodeStatus() (tea.Model, tea.Cmd) {
	info, err := getInfo(CurrentConfig.NodeRPCPort)
	if err != nil {
		m.nodeError = fmt.Sprintf("No node on :%d — %v", CurrentConfig.NodeRPCPort, err)
		m.currentView = viewNodeInfo
		return m, nil
	}
	m.runningNode = true
	m.nodeExternal = m.nodeCmd == nil
	m.nodeError = ""
	m.populateNodeInfo(info)
	m.statusMsg = "Node detected"
	m.currentView = viewNodeInfo
	return m, nil
}

func (m model) createWallet() (tea.Model, tea.Cmd) {
	if m.runningW {
		m.appendLog("Wallet RPC already running — close it first")
		m.statusMsg = "Wallet already running"
		return m, nil
	}
	if _, err := os.Stat(m.containerFile); err == nil {
		m.appendLog("Wallet container exists at " + m.containerFile + " — use Open Wallet")
		m.statusMsg = "Container exists"
		return m, nil
	}

	path := binPath("walletd")
	if path == "walletd" {
		if _, err := exec.LookPath("walletd"); err != nil {
			m.appendLog("walletd not found — build it first")
			m.statusMsg = "walletd not found"
			return m, nil
		}
	}

	m.containerPass = fmt.Sprintf("xfg%d", time.Now().Unix()%100000)
	savePassword(m.containerFile, m.containerPass)
	dd := dataDir()
	os.MkdirAll(dd, 0755)

	args := []string{
		"-g",
		"-w", m.containerFile,
		"-p", m.containerPass,
		fmt.Sprintf("--data-dir=%s", dd),
		"--daemon-port", fmt.Sprintf("%d", CurrentConfig.NodeRPCPort),
		"--daemon-address", "127.0.0.1",
	}
	if CurrentConfig.IsTestnet {
		args = append(args, "--testnet")
	}

	m.appendLog("Creating wallet container...")
	cmd := exec.Command(path, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		m.appendLog("walletd create failed: " + err.Error() + "\n" + string(out))
		m.statusMsg = "Create wallet failed"
		m.containerPass = ""
		return m, nil
	}

	m.appendLog("Container created: " + m.containerFile)

	rpcArgs := []string{
		"-w", m.containerFile,
		"-p", m.containerPass,
		fmt.Sprintf("--data-dir=%s", dd),
		"--daemon-port", fmt.Sprintf("%d", CurrentConfig.NodeRPCPort),
		"--daemon-address", "127.0.0.1",
		"--bind-port", fmt.Sprintf("%d", m.walletPort),
		"--bind-address", "127.0.0.1",
	}
	if CurrentConfig.IsTestnet {
		rpcArgs = append(rpcArgs, "--testnet")
	}

	walletCmd := exec.Command(path, rpcArgs...)
	wStdout, _ := walletCmd.StdoutPipe()
	wStderr, _ := walletCmd.StderrPipe()
	if err := walletCmd.Start(); err != nil {
		m.appendLog("Failed to start walletd: " + err.Error())
		m.statusMsg = "walletd start failed"
		return m, nil
	}
	m.walletCmd = walletCmd
	m.runningW = true
	m.walletStdout = wStdout
	m.walletStderr = wStderr
	m.appendLog(fmt.Sprintf("walletd started on port %d", m.walletPort))
	m.statusMsg = "Wallet RPC starting..."
	m.currentView = viewWalletInfo

	time.Sleep(2 * time.Second)
	return m, tea.Batch(
		streamPipe(wStdout, "WALLET"),
		streamPipe(wStderr, "WALLET-ERR"),
		walletRpcCmd(m.walletPort, "createAddress", map[string]interface{}{}),
	)
}

func (m model) openWallet() (tea.Model, tea.Cmd) {
	if m.runningW {
		m.appendLog("Wallet RPC already running")
		m.statusMsg = "Wallet already running"
		return m, nil
	}

	if _, err := os.Stat(m.containerFile); os.IsNotExist(err) {
		m.appendLog("No wallet container found at " + m.containerFile + " — use Create Wallet first")
		m.statusMsg = "No container"
		return m, nil
	}

	path := binPath("walletd")
	if path == "walletd" {
		if _, err := exec.LookPath("walletd"); err != nil {
			m.appendLog("walletd not found")
			m.statusMsg = "walletd not found"
			return m, nil
		}
	}

	if m.containerPass == "" {
		m.containerPass = loadPassword(m.containerFile)
	}
	if m.containerPass == "" {
		m.appendLog("No container password found — use Create Wallet first")
		m.statusMsg = "No password"
		return m, nil
	}

	dd := dataDir()
	os.MkdirAll(dd, 0755)

	args := []string{
		"-w", m.containerFile,
		"-p", m.containerPass,
		fmt.Sprintf("--data-dir=%s", dd),
		"--daemon-port", fmt.Sprintf("%d", CurrentConfig.NodeRPCPort),
		"--daemon-address", "127.0.0.1",
		"--bind-port", fmt.Sprintf("%d", m.walletPort),
		"--bind-address", "127.0.0.1",
	}
	if CurrentConfig.IsTestnet {
		args = append(args, "--testnet")
	}

	walletCmd := exec.Command(path, args...)
	wStdout, _ := walletCmd.StdoutPipe()
	wStderr, _ := walletCmd.StderrPipe()
	if err := walletCmd.Start(); err != nil {
		m.appendLog("Failed to start walletd: " + err.Error())
		m.statusMsg = "walletd start failed"
		return m, nil
	}
	m.walletCmd = walletCmd
	m.runningW = true
	m.walletStdout = wStdout
	m.walletStderr = wStderr
	m.appendLog(fmt.Sprintf("walletd started on port %d", m.walletPort))
	m.statusMsg = "Wallet RPC starting..."
	m.currentView = viewWalletInfo

	time.Sleep(2 * time.Second)
	return m, tea.Batch(
		streamPipe(wStdout, "WALLET"),
		streamPipe(wStderr, "WALLET-ERR"),
		walletRpcCmd(m.walletPort, "getAddresses", map[string]interface{}{}),
	)
}

func (m model) getBalance() (tea.Model, tea.Cmd) {
	if !m.runningW {
		m.appendLog("Wallet RPC not running")
		m.statusMsg = "Wallet offline"
		return m, nil
	}
	m.currentView = viewWalletInfo
	return m, walletRpcCmd(m.walletPort, "getBalance", map[string]interface{}{})
}

func (m model) sendTx() (tea.Model, tea.Cmd) {
	if !m.runningW {
		m.appendLog("Wallet RPC not running")
		m.statusMsg = "Wallet offline"
		return m, nil
	}
	if m.walletAddress == "" {
		m.appendLog("No wallet address — create wallet first")
		m.statusMsg = "No wallet"
		return m, nil
	}

	amtFloat, err := strconv.ParseFloat(m.txAmt, 64)
	if err != nil || amtFloat <= 0 {
		m.appendLog("Invalid amount: " + m.txAmt)
		m.statusMsg = "Invalid amount"
		return m, nil
	}
	amountAtomic := int64(amtFloat * float64(CurrentConfig.CoinUnits))

	m.appendLog(fmt.Sprintf("Sending %.6f XFG to %s...", amtFloat, m.txAddr))
	params := map[string]interface{}{
		"sourceAddresses": []string{m.walletAddress},
		"transfers": []map[string]interface{}{
			{"address": m.txAddr, "amount": amountAtomic},
		},
		"changeAddress": m.walletAddress,
		"anonymity":     4,
	}
	return m, walletRpcCmd(m.walletPort, "sendTransaction", params)
}

// ── Tea.Cmd Helpers ─────────────────────────────────────────────────────────

func streamPipe(r io.Reader, prefix string) tea.Cmd {
	return func() tea.Msg {
		buf := make([]byte, 4096)
		n, err := r.Read(buf)
		if err != nil {
			return pipeDoneMsg{prefix: prefix, err: err}
		}
		return pipeDataMsg{prefix: prefix, data: string(buf[:n])}
	}
}

func reschedulePipe(m *model, prefix string) tea.Cmd {
	switch prefix {
	case "NODE":
		if m.nodeStdout != nil {
			return streamPipe(m.nodeStdout, prefix)
		}
	case "NODE-ERR":
		if m.nodeStderr != nil {
			return streamPipe(m.nodeStderr, prefix)
		}
	case "WALLET":
		if m.walletStdout != nil {
			return streamPipe(m.walletStdout, prefix)
		}
	case "WALLET-ERR":
		if m.walletStderr != nil {
			return streamPipe(m.walletStderr, prefix)
		}
	}
	return nil
}

func pollNodeTick() tea.Cmd {
	return func() tea.Msg {
		time.Sleep(8 * time.Second)
		return tickMsg{}
	}
}

func pollNodeCmd() tea.Cmd {
	return func() tea.Msg {
		info, err := getInfo(CurrentConfig.NodeRPCPort)
		return nodePollMsg{info: info, err: err}
	}
}

func walletRpcCmd(port int, method string, params map[string]interface{}) tea.Cmd {
	return func() tea.Msg {
		result, err := doWalletRpc(port, method, params)
		return rpcResultMsg{method: method, result: result, err: err}
	}
}

func daemonGetCmd(path string) tea.Cmd {
	return func() tea.Msg {
		url := fmt.Sprintf("http://127.0.0.1:%d%s", CurrentConfig.NodeRPCPort, path)
		client := http.Client{Timeout: 5 * time.Second}
		resp, err := client.Get(url)
		if err != nil {
			return daemonResultMsg{path: path, err: fmt.Errorf("connect: %w", err)}
		}
		defer resp.Body.Close()
		body, _ := io.ReadAll(resp.Body)
		var out map[string]interface{}
		if err := json.Unmarshal(body, &out); err != nil {
			return daemonResultMsg{path: path, err: fmt.Errorf("json: %w", err)}
		}
		return daemonResultMsg{path: path, result: out}
	}
}

func doWalletRpc(port int, method string, params map[string]interface{}) (map[string]interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", port)
	payload := map[string]interface{}{
		"jsonrpc": "2.0", "id": "0", "method": method, "params": params,
	}
	b, err := json.Marshal(payload)
	if err != nil {
		return nil, fmt.Errorf("marshal: %w", err)
	}

	client := http.Client{Timeout: 15 * time.Second}
	resp, err := client.Post(url, "application/json", bytes.NewReader(b))
	if err != nil {
		return nil, fmt.Errorf("connect: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, string(body))
	}

	var out map[string]interface{}
	if err := json.Unmarshal(body, &out); err != nil {
		return nil, fmt.Errorf("json: %w", err)
	}

	if errObj, ok := out["error"].(map[string]interface{}); ok {
		if msg, ok := errObj["message"].(string); ok {
			return nil, fmt.Errorf("rpc error: %s", msg)
		}
		return nil, fmt.Errorf("rpc error: %v", errObj)
	}

	if res, ok := out["result"].(map[string]interface{}); ok {
		return res, nil
	}
	return out, nil
}

type nodeInfo struct {
	Height    int    `json:"height"`
	Peers     int    `json:"peers"`
	Incoming  int    `json:"incoming_connections_count"`
	Outgoing  int    `json:"outgoing_connections_count"`
	Difficulty int64 `json:"difficulty"`
	TxCount   int    `json:"tx_count"`
	TxPool    int    `json:"tx_pool_size"`
	Version   string `json:"version"`
	TopHash   string `json:"top_block_hash"`
}

func getInfo(port int) (nodeInfo, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/getinfo", port)
	client := http.Client{Timeout: 3 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return nodeInfo{}, err
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var out map[string]interface{}
	json.Unmarshal(body, &out)

	ni := nodeInfo{}
	if h, ok := out["height"].(float64); ok {
		ni.Height = int(h)
	}
	if h, ok := out["difficulty"].(float64); ok {
		ni.Difficulty = int64(h)
	}
	if h, ok := out["tx_count"].(float64); ok {
		ni.TxCount = int(h)
	}
	if h, ok := out["tx_pool_size"].(float64); ok {
		ni.TxPool = int(h)
	}
	if h, ok := out["incoming_connections_count"].(float64); ok {
		ni.Incoming = int(h)
	}
	if h, ok := out["outgoing_connections_count"].(float64); ok {
		ni.Outgoing = int(h)
	}
	if h, ok := out["version"].(string); ok {
		ni.Version = h
	}
	if h, ok := out["top_block_hash"].(string); ok {
		ni.TopHash = h
	}
	ni.Peers = ni.Incoming + ni.Outgoing
	return ni, nil
}

func valueInt(m map[string]interface{}, key string) int64 {
	if v, ok := m[key]; ok {
		switch n := v.(type) {
		case float64:
			return int64(n)
		case json.Number:
			if i, err := n.Int64(); err == nil {
				return i
			}
		}
	}
	return 0
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}

func main() {
	isTestnet := flag.Bool("testnet", false, "Run TUI in Testnet mode")
	nodePort := flag.Int("node-port", 0, "Override daemon RPC port (default: 18180 mainnet, 28280 testnet)")
	flag.Parse()

	if *isTestnet {
		CurrentConfig = TestnetConfig
	} else {
		CurrentConfig = MainnetConfig
	}
	if *nodePort > 0 {
		CurrentConfig.NodeRPCPort = *nodePort
	}

	fmt.Printf("=== %s TUI ===\n", CurrentConfig.NetworkName)
	fmt.Printf("Coin: %s | Prefix: %s | Mode: %s\n", CurrentConfig.CoinName, CurrentConfig.AddressPrefix, CurrentConfig.NetworkName)
	fmt.Printf("Daemon: %s (RPC %d, P2P %d)\n", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort, CurrentConfig.NodeP2PPort)

	p := tea.NewProgram(initialModel())
	if _, err := p.Run(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}
