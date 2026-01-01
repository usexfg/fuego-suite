package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

const (
	nodeRPCPort   = 28280
	walletRPCPort = 28283
	nodeP2PPort   = 20808
)

var (
	titleStyle  = lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("205"))
	menuStyle   = lipgloss.NewStyle().Padding(1, 2)
	activeStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("39"))
	logStyle    = lipgloss.NewStyle().Foreground(lipgloss.Color("250"))
)

type versionInfo struct {
	projectName    string
	projectVersion string
	testnetVersion string
	fullVersion    string
}

var verInfo = versionInfo{
	projectName: "Fuego DYNAMIGO",
	fullVersion: "Fuego DYNAMIGO TESTNET",
}

type menuItem string

const (
	mStartNode        menuItem = "Start Node"
	mStopNode         menuItem = "Stop Node"
	mNodeStatus       menuItem = "Node Status"
	mStartWalletRPC   menuItem = "Start Wallet RPC"
	mCreateWallet     menuItem = "Create Wallet"
	mGetBalance       menuItem = "Get Balance"
	mSendTx           menuItem = "Send Test TX (1 XFG)"
	mElderfierStayking menuItem = "Start Elderfier Stake (80 XFG)"
	mCheckStake       menuItem = "Check Stake Status"
	mViewConsensus    menuItem = "View Consensus"
	mVoteOnPending    menuItem = "Vote on Pending"
	mBurn08           menuItem = "Burn 0.8 XFG"
	mBurn800          menuItem = "Burn 800 XFG"
	mShowLogs         menuItem = "Show Logs"
	mQuit             menuItem = "Quit"
)

var menu = []menuItem{
	mStartNode,
	mStopNode,
	mNodeStatus,
	mStartWalletRPC,
	mCreateWallet,
	mGetBalance,
	mSendTx,
	mElderfierStayking,
	mCheckStake,
	mViewConsensus,
	mVoteOnPending,
	mBurn08,
	mBurn800,
	mShowLogs,
	mQuit,
}

type model struct {
	cursor      int
	nodeCmd     *exec.Cmd
	walletCmd   *exec.Cmd
	logs        []string
	mutex       sync.Mutex
	statusMsg   string
	runningNode bool
	runningW    bool
	showingLogs bool
	logOffset   int
	height      int
	peers       int
}

func initialModel() model {
	initVersionInfo()
	return model{
		cursor:      0,
		logs:        []string{"🔥 FUEGO Testnet TUI Ready - " + verInfo.fullVersion},
		statusMsg:   "Ready - arrows navigate, Enter selects",
		showingLogs: false,
		logOffset:   0,
	}
}

func (m *model) appendLog(s string) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	m.logs = append(m.logs, fmt.Sprintf("%s %s", time.Now().Format("15:04:05"), s))
	if len(m.logs) > 200 {
		m.logs = m.logs[len(m.logs)-200:]
	}
}

func (m model) Init() tea.Cmd {
	return nil
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	// Handle log viewing mode
	if m.showingLogs {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "q", "esc", "enter", " ":
				m.showingLogs = false
				m.logOffset = 0
				return m, nil
			case "up", "k":
				if m.logOffset > 0 {
					m.logOffset--
				}
				return m, nil
			case "down", "j":
				maxOffset := len(m.logs) - 1
				if m.logOffset < maxOffset {
					m.logOffset++
				}
				return m, nil
			}
		}
		return m, nil
	}

	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "up", "k":
			if m.cursor > 0 {
				m.cursor--
			}
		case "down", "j":
			if m.cursor < len(menu)-1 {
				m.cursor++
			}
		case "enter":
			item := menu[m.cursor]
			switch item {
			case mStartNode:
				m = startNode(m)
			case mStopNode:
				m = stopNode(m)
			case mNodeStatus:
				m = queryNodeStatus(m)
			case mStartWalletRPC:
				m = startWalletRPC(m)
			case mCreateWallet:
				m = createWalletCmd(m)
			case mGetBalance:
				m = getBalanceCmd(m)
			case mSendTx:
				m = sendTestTx(m)
			case mElderfierStayking:
				m = startElderfierStayking(m)
			case mCheckStake:
				m = checkStakeStatus(m)
			case mViewConsensus:
				m = viewConsensusRequests(m)
			case mVoteOnPending:
				m = voteOnPendingItems(m)
			case mBurn08:
				m = burnXFG(m, 0.8)
			case mBurn800:
				m = burnXFG(m, 800.0)
			case mShowLogs:
				if len(m.logs) > 0 {
					m.showingLogs = true
					m.logOffset = len(m.logs) - 1
				}
				return m, nil
			case mQuit:
				return m, tea.Quit
			}
		case "l":
			if len(m.logs) > 0 {
				m.showingLogs = true
				m.logOffset = len(m.logs) - 1
			}
			return m, nil
		case "q", "ctrl+c":
			return m, tea.Quit
		}
	}
	return m, nil
}

func (m model) View() string {
	// Show logs view
	if m.showingLogs {
		s := titleStyle.Render(fmt.Sprintf("🔥 %s - LOGS", verInfo.projectName)) + "\n"
		s += menuStyle.Render("Press q/esc/enter to return | j/k to scroll") + "\n\n"
		start := len(m.logs) - 50
		if start < 0 {
			start = 0
		}
		logs := m.logs[start:]
		if m.logOffset >= start {
			logs = m.logs[start : m.logOffset+1]
		}
		s += lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Render(strings.Join(logs, "\n"))
		return s
	}

	// Main menu view
	s := titleStyle.Render(fmt.Sprintf("🔥 %s TESTNET TUI", verInfo.projectName)) + "\n"
	s += menuStyle.Render(verInfo.fullVersion) + "\n\n"
	for i, it := range menu {
		line := fmt.Sprintf("  %s", it)
		if m.cursor == i {
			line = activeStyle.Render(line)
		}
		s += menuStyle.Render(line) + "\n"
	}
	s += "\n" + lipgloss.NewStyle().Foreground(lipgloss.Color("250")).Render("Status: "+m.statusMsg) + "\n"
	if m.runningNode {
		s += lipgloss.NewStyle().Render(fmt.Sprintf("Node: running | Height: %d | Peers: %d\n", m.height, m.peers))
	} else {
		s += lipgloss.NewStyle().Render("Node: stopped\n")
	}
	s += lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Render("Press 'l' to view logs, 'q' to quit")
	return s
}

func binPath(name string) string {
	// Hardcoded path to testnet binaries
	binPath := filepath.Join("/home/ar/fuego", "build", "release", "src", name)
	if _, err := os.Stat(binPath); err == nil {
		return binPath
	}
	return name
}

func startNode(m model) model {
	if m.runningNode {
		m.appendLog("Node already running")
		m.statusMsg = "Node already running"
		return m
	}
	path := binPath("testnetd")
	if path == "testnetd" {
		m.appendLog("testnetd binary not found")
		m.statusMsg = "Binary not found"
		return m
	}
	dataDir := filepath.Join(os.Getenv("HOME"), ".fuego-testnet")
	os.MkdirAll(dataDir, 0755)
	cmd := exec.Command(path,
		"--data-dir", dataDir,
		"--rpc-bind-port", fmt.Sprintf("%d", nodeRPCPort),
		"--p2p-bind-port", fmt.Sprintf("%d", nodeP2PPort),
	)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		m.appendLog("Failed to start node: " + err.Error())
		m.statusMsg = "Failed to start node"
		return m
	}
	m.nodeCmd = cmd
	m.runningNode = true
	m.appendLog("Started testnetd: " + path)
	m.statusMsg = "Node starting..."
	go streamPipe(stdout, "NODE", &m)
	go streamPipe(stderr, "NODE-ERR", &m)
	time.Sleep(3 * time.Second)
	go func() {
		for m.runningNode {
			info, err := getInfo(nodeRPCPort)
			if err == nil {
				m.height = info.Height
				m.peers = info.Peers
				m.statusMsg = fmt.Sprintf("Node running — height %d", m.height)
			} else {
				m.appendLog("Node query failed: " + err.Error())
			}
			time.Sleep(5 * time.Second)
		}
	}()
	return m
}

func stopNode(m model) model {
	if !m.runningNode || m.nodeCmd == nil {
		m.appendLog("Node not running")
		m.statusMsg = "Node not running"
		return m
	}
	_ = m.nodeCmd.Process.Kill()
	m.appendLog("Stopped testnetd")
	m.nodeCmd = nil
	m.runningNode = false
	m.statusMsg = "Node stopped"
	return m
}

func queryNodeStatus(m model) model {
	info, err := getInfo(nodeRPCPort)
	if err != nil {
		m.appendLog("Failed to query node: " + err.Error())
		m.statusMsg = "Query failed"
		return m
	}
	m.appendLog(fmt.Sprintf("Height: %d, Peers: %d", info.Height, info.Peers))
	m.statusMsg = "Status fetched"
	m.height = info.Height
	m.peers = info.Peers
	return m
}

func startWalletRPC(m model) model {
	if m.runningW {
		m.appendLog("Wallet RPC already running")
		m.statusMsg = "Wallet RPC already running"
		return m
	}
	path := binPath("walletd")
	if path == "walletd" {
		m.appendLog("walletd binary not found")
		m.statusMsg = "Binary not found"
		return m
	}
	dataDir := filepath.Join(os.Getenv("HOME"), ".fuego-testnet")
	os.MkdirAll(dataDir, 0755)
	cmd := exec.Command(path,
		"--daemon-port", fmt.Sprintf("%d", nodeRPCPort),
		"--bind-address", "127.0.0.1",
		"--bind-port", fmt.Sprintf("%d", walletRPCPort),
		"--data-dir", dataDir,
	)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		m.appendLog("Failed to start walletd: " + err.Error())
		m.statusMsg = "Failed to start walletd"
		return m
	}
	m.walletCmd = cmd
	m.runningW = true
	m.appendLog("Started walletd: " + path)
	m.statusMsg = "Wallet RPC starting..."
	go streamPipe(stdout, "WALLET", &m)
	go streamPipe(stderr, "WALLET-ERR", &m)
	return m
}

func createWalletCmd(m model) model {
	m.appendLog("Creating wallet...")
	_, err := walletRpcCall(walletRPCPort, "create_address", map[string]interface{}{})
	if err != nil {
		m.appendLog("Create wallet error: " + err.Error())
		m.statusMsg = "Create wallet failed"
		return m
	}
	m.appendLog("Wallet creation requested (check walletd output)")
	m.statusMsg = "Wallet created"
	return m
}

func getBalanceCmd(m model) model {
	m.appendLog("Querying balance...")
	res, err := walletRpcCall(walletRPCPort, "get_balance", map[string]interface{}{})
	if err != nil {
		m.appendLog("Balance error: " + err.Error())
		m.statusMsg = "Get balance failed"
		return m
	}
	m.appendLog("Balance: " + fmt.Sprintf("%v", res))
	m.statusMsg = "Balance fetched"
	return m
}

func sendTestTx(m model) model {
	m.appendLog("Sending test transaction (1 XFG to same wallet)...")
	addrRes, err := walletRpcCall(walletRPCPort, "getAddresses", map[string]interface{}{})
	if err != nil {
		m.appendLog("Get address failed: " + err.Error())
		m.statusMsg = "Get address failed"
		return m
	}
	addr := fmt.Sprintf("%v", addrRes)
	if addr == "" {
		m.appendLog("No wallet address found")
		m.statusMsg = "No wallet"
		return m
	}
	amountAtomic := int64(1 * 100000000)
	params := map[string]interface{}{"transfers": []map[string]interface{}{{"address": addr, "amount": amountAtomic}}}
	res, err := walletRpcCall(walletRPCPort, "send_transaction", params)
	if err != nil {
		m.appendLog("Send TX error: " + err.Error())
		m.statusMsg = "Send failed"
		return m
	}
	m.appendLog("TX sent: " + fmt.Sprintf("%v", res))
	m.statusMsg = "Test TX sent"
	return m
}

func startElderfierStayking(m model) model {
	m.appendLog("Creating stake deposit (80 XFG)...")
	params := map[string]interface{}{
		"amount": int64(80 * 100000000),
		"type":   "elderfier_stake",
	}
	res, err := walletRpcCall(walletRPCPort, "create_stake_deposit", params)
	if err != nil {
		m.appendLog("Stake error: " + err.Error())
		m.statusMsg = "Stake failed"
		return m
	}
	txHash := fmt.Sprintf("%v", res["tx_hash"])
	m.appendLog("Stake TX: " + txHash)
	m.appendLog("Now: 1) Wait 10 blocks 2) Use register_to_enindex with your elder ID")
	m.statusMsg = "Stake created"
	return m
}

func checkStakeStatus(m model) model {
	m.appendLog("Checking stake status...")
	res, err := walletRpcCall(walletRPCPort, "get_stake_status", map[string]interface{}{})
	if err != nil {
		m.appendLog("No stake: " + err.Error())
		m.statusMsg = "No stake"
	} else {
		m.appendLog("Stake: " + fmt.Sprintf("%v", res))
		m.statusMsg = "Stake checked"
	}
	return m
}

func viewConsensusRequests(m model) model {
	m.appendLog("Querying consensus requests...")
	res, err := walletRpcCall(walletRPCPort, "get_consensus_requests", map[string]interface{}{})
	if err != nil {
		m.appendLog("Consensus unavailable: " + err.Error())
		m.statusMsg = "Query failed"
	} else {
		m.appendLog("Requests: " + fmt.Sprintf("%v", res))
		m.statusMsg = "Consensus fetched"
	}
	return m
}

func voteOnPendingItems(m model) model {
	m.appendLog("Checking pending votes...")
	res, err := walletRpcCall(walletRPCPort, "get_pending_votes", map[string]interface{}{})
	if err != nil {
		m.appendLog("No pending votes: " + err.Error())
		m.statusMsg = "No votes"
	} else {
		m.appendLog("Pending: " + fmt.Sprintf("%v", res))
		m.appendLog("Use submit_vote RPC to vote")
		m.statusMsg = "Votes listed"
	}
	return m
}

func burnXFG(m model, amount float64) model {
	m.appendLog(fmt.Sprintf("Creating burn deposit (%.2f XFG)...", amount))
	amountAtomic := int64(amount * 100000000)
	params := map[string]interface{}{"amount": amountAtomic}
	res, err := walletRpcCall(walletRPCPort, "create_burn_deposit", params)
	if err != nil {
		m.appendLog("Burn error: " + err.Error())
		m.statusMsg = "Burn failed"
		return m
	}
	txHash := fmt.Sprintf("%v", res["tx_hash"])
	m.appendLog("Burn TX: " + txHash)
	m.appendLog("Wait 10 blocks, then request_elderfier_consensus")
	m.statusMsg = "Burn created"
	return m
}

func streamPipe(r io.Reader, prefix string, m *model) {
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" {
			m.appendLog(fmt.Sprintf("%s: %s", prefix, line))
		}
	}
	if err := scanner.Err(); err != nil && err != io.EOF {
		m.appendLog(prefix + " error: " + err.Error())
	}
}

type nodeInfo struct {
	Height int `json:"height"`
	Peers  int `json:"peers"`
}

func getInfo(port int) (nodeInfo, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/get_info", port)
	client := http.Client{Timeout: 3 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return nodeInfo{}, err
	}
	defer resp.Body.Close()
	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nodeInfo{}, err
	}
	height := 0
	peers := 0
	if h, ok := out["height"].(float64); ok {
		height = int(h)
	}
	if h, ok := out["height"].(int); ok {
		height = h
	}
	if p, ok := out["incoming_connections_count"].(float64); ok {
		peers += int(p)
	}
	if p, ok := out["incoming_connections_count"].(int); ok {
		peers += p
	}
	if p, ok := out["outgoing_connections_count"].(float64); ok {
		peers += int(p)
	}
	if p, ok := out["outgoing_connections_count"].(int); ok {
		peers += p
	}
	return nodeInfo{Height: height, Peers: peers}, nil
}

func walletRpcCall(port int, method string, params map[string]interface{}) (map[string]interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", port)
	payload := map[string]interface{}{"jsonrpc": "2.0", "id": "0", "method": method, "params": params}
	b, _ := json.Marshal(payload)
	client := http.Client{Timeout: 4 * time.Second}
	resp, err := client.Post(url, "application/json", bytes.NewReader(b))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	if res, ok := out["result"].(map[string]interface{}); ok {
		return res, nil
	}
	return out, nil
}

func main() {
	model := initialModel()
	program := tea.NewProgram(model)
	if _, err := program.Run(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}

func initVersionInfo() {
	versionFile := filepath.Join("/home/ar/fuego", "build", "release", "version", "version.h")
	if content, err := os.ReadFile(versionFile); err == nil {
		verStr := string(content)
		reVer := regexp.MustCompile(`#define PROJECT_VERSION "([^"]+)"`)
		if verMatch := reVer.FindStringSubmatch(verStr); len(verMatch) > 1 {
			verInfo.projectVersion = verMatch[1]
		}
		reBuild := regexp.MustCompile(`#define BUILD_VERSION "([^"]+)"`)
		if buildMatch := reBuild.FindStringSubmatch(verStr); len(buildMatch) > 1 {
			verInfo.projectVersion = fmt.Sprintf("%s.%s", verInfo.projectVersion, buildMatch[1])
		}
		reCommit := regexp.MustCompile(`#define BUILD_COMMIT_ID "([^"]+)"`)
		if commitMatch := reCommit.FindStringSubmatch(verStr); len(commitMatch) > 1 {
			commit := commitMatch[1]
			if commit != "@VERSION@" {
				verInfo.projectVersion = fmt.Sprintf("%s(%s)", verInfo.projectVersion, commit)
			}
		}
		verInfo.testnetVersion = "TESTNET v." + verInfo.projectVersion
		verInfo.fullVersion = fmt.Sprintf("%s TESTNET", verInfo.projectName)
	} else {
		verInfo.fullVersion = "Fuego DYNAMIGO TESTNET (dev)"
	}
}
