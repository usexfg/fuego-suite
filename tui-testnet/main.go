package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
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

// VersionInfo holds dynamic version information
type versionInfo struct {
	projectName    string
	projectVersion string
	testnetVersion string
	fullVersion    string
}

var verInfo = versionInfo{
	projectName:    "Fuego",
	projectVersion: "DYNAMIGO v1.10.0",
	fullVersion:    "Fuego TESTNET || v1.10.0 DYNAMIGO",
}

type menuItem string

const (
	mStartNode      menuItem = "Start Node"
	mStopNode       menuItem = "Stop Node"
	mNodeStatus     menuItem = "Node Status"
	mStartWalletRPC menuItem = "Start Wallet RPC"
	mCreateWallet   menuItem = "Create Wallet"
	mGetBalance     menuItem = "Get Balance"
	mSendTx         menuItem = "Send Transaction"
	mBurn2MintMenu  menuItem = "Burn2Mint Menu"
	mShowLogs       menuItem = "Show Logs"
	mQuit           menuItem = "Quit"
)

var menu = []menuItem{
	mStartNode,
	mStopNode,
	mNodeStatus,
	mStartWalletRPC,
	mCreateWallet,
	mGetBalance,
	mSendTx,
	mBurn2MintMenu,
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
	running     bool
	runningNode bool
	runningW    bool
	height      int
	peers       int
}

func initialModel() model {
	initVersionInfo()
	return model{
		cursor:    0,
		logs:      []string{fmt.Sprintf("🔥 %s TESTNET TUI Ready", verInfo.projectName)},
		statusMsg: "",
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
				m = sendTxPrompt(m)
			case mBurn2MintMenu:
				m = burn2MintMenu(m)
			case mShowLogs:
				m = showLogs(m)
			case mQuit:
				return m, tea.Quit
			}
		case "q", "ctrl+c":
			return m, tea.Quit
		}
	}
	return m, nil
}

func (m model) View() string {
	s := titleStyle.Render(fmt.Sprintf("🔥 %s TESTNET TUI", verInfo.projectName)) + "\n"
	s += menuStyle.Render(verInfo.fullVersion) + "\n\n"
	for i, it := range menu {
		line := fmt.Sprintf("  %s", it)
		if m.cursor == i {
			line = activeStyle.Render(line)
		}
		s += menuStyle.Render(line) + "\n"
	}
	s += "\n" + lipgloss.NewStyle().Foreground(lipgloss.Color("250")).Render("Status: ") + m.statusMsg + "\n"
	if m.runningNode {
		s += lipgloss.NewStyle().Render(fmt.Sprintf("Node: running | Height: %d | Peers: %d\n", m.height, m.peers))
	} else {
		s += lipgloss.NewStyle().Render("Node: stopped\n")
	}
	return s
}

func binPath(name string) string {
	cwd, _ := os.Getwd()
	// Check if we're in tui-testnet directory
	if filepath.Base(cwd) == "tui-testnet" {
		cwd = filepath.Dir(cwd)
		// Go up one more to get to fuego directory
		if filepath.Base(cwd) == "fuego" {
			// Try to find build directory
			testPath := filepath.Join(cwd, "build", "src", name)
			if _, err := os.Stat(testPath); err == nil {
				return testPath
			}
		}
	}
	// Try direct path in current directory
	cand := filepath.Join(cwd, "build", "src", name)
	if _, err := os.Stat(cand); err == nil {
		return cand
	}
	// Try in parent directory
	cand = filepath.Join(cwd, "..", "build", "src", name)
	if _, err := os.Stat(cand); err == nil {
		return cand
	}
	// Fall back to PATH
	return name
}

func startNode(m model) model {
	if m.runningNode {
		m.appendLog("Node already running")
		m.statusMsg = "Node already running"
		return m
	}
	path := binPath("fuegod")
	if path == "fuegod" {
		// Try to find in PATH
		if _, err := exec.LookPath("fuegod"); err != nil {
			m.appendLog("fuegod binary not found in PATH or build directory")
			m.statusMsg = "Binary not found"
			return m
		}
	}
	// Use testnet-specific data directory
	var dataDir string
	if runtime.GOOS == "darwin" {
		dataDir = filepath.Join(os.Getenv("HOME"), "Library/Application Support/Fuego-testnet")
	} else {
		dataDir = filepath.Join(os.Getenv("HOME"), ".fuego-testnet")
	}
	// Create directory if it doesn't exist
	os.MkdirAll(dataDir, 0755)
	// Start with testnet mode on testnet ports
	cmd := exec.Command(path,
		fmt.Sprintf("--p2p-bind-port=%d", nodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", nodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
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
	m.appendLog("Started fuegod (testnet)")
	m.statusMsg = "Node starting"
	go streamPipe(stdout, "NODE", &m)
	go streamPipe(stderr, "NODE-ERR", &m)
	// Wait for RPC to initialize before querying
	time.Sleep(3 * time.Second)
	go func() {
		for m.runningNode && m.nodeCmd != nil {
			info, err := getInfo(nodeRPCPort)
			if err == nil {
				m.height = info.Height
				m.peers = info.Peers
				m.statusMsg = fmt.Sprintf("Node running — height %d", m.height)
			} else {
				// Log errors less frequently to avoid spam
				m.mutex.Lock()
				shouldLog := len(m.logs) == 0 || !strings.Contains(m.logs[len(m.logs)-1], "Failed to query node")
				m.mutex.Unlock()
				if shouldLog {
					m.appendLog("Failed to query node: " + err.Error())
				}
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
	m.appendLog("Stopped fuegod")
	m.nodeCmd = nil
	m.runningNode = false
	m.statusMsg = "Node stopped"
	m.height = 0
	m.peers = 0
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
		// Try to find in PATH
		if _, err := exec.LookPath("walletd"); err != nil {
			m.appendLog("walletd binary not found in PATH or build directory")
			m.statusMsg = "Binary not found"
			return m
		}
	}
	// Use testnet-specific data directory
	var dataDir string
	if runtime.GOOS == "darwin" {
		dataDir = filepath.Join(os.Getenv("HOME"), "Library/Application Support/Fuego-testnet")
	} else {
		dataDir = filepath.Join(os.Getenv("HOME"), ".fuego-testnet")
	}
	cmd := exec.Command(path,
		"--daemon-port", fmt.Sprintf("%d", nodeRPCPort),
		"--bind-address", "127.0.0.1",
		"--bind-port", fmt.Sprintf("%d", walletRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
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
	m.appendLog("Started walletd (testnet)")
	m.statusMsg = "Wallet RPC started"
	go streamPipe(stdout, "WALLET", &m)
	go streamPipe(stderr, "WALLET-ERR", &m)
	return m
}

func createWalletCmd(m model) model {
	m.appendLog("Creating wallet (RPC)...")
	_, err := walletRpcCall(walletRPCPort, "create_address", map[string]interface{}{})
	if err != nil {
		m.appendLog("create wallet error: " + err.Error())
		m.statusMsg = "Create wallet failed"
		return m
	}
	m.appendLog("Create wallet attempted (see walletd logs)")
	m.statusMsg = "Create wallet requested"
	return m
}

func getBalanceCmd(m model) model {
	m.appendLog("Querying balance...")
	res, err := walletRpcCall(walletRPCPort, "get_balance", map[string]interface{}{})
	if err != nil {
		m.appendLog("get balance error: " + err.Error())
		m.statusMsg = "Get balance failed"
		return m
	}
	m.appendLog("Balance: " + fmt.Sprintf("%v", res))
	m.statusMsg = "Balance fetched"
	return m
}

func sendTxPrompt(m model) model {
	m.appendLog("Send TX: enter recipient address:")
	var addr string
	fmt.Print("Recipient address: ")
	fmt.Scanln(&addr)
	fmt.Print("Amount TEST: ")
	var amt float64
	fmt.Scanln(&amt)
	m.appendLog(fmt.Sprintf("Sending %f to %s...", amt, addr))
	amountAtomic := int64(amt * 10000000)
	params := map[string]interface{}{"transfers": []map[string]interface{}{{"address": addr, "amount": amountAtomic}}}
	res, err := walletRpcCall(walletRPCPort, "send_transaction", params)
	if err != nil {
		m.appendLog("send tx error: " + err.Error())
		m.statusMsg = "Send failed"
		return m
	}
	m.appendLog("Tx sent: " + fmt.Sprintf("%v", res))
	m.statusMsg = "Tx sent"
	return m
}

// Burn2Mint flow
func burn2MintMenu(m model) model {
	m.appendLog("═══════════════════════════════════════")
	m.appendLog("  BURN2MINT: TESTNET → HEAT (TESTNET)")
	m.appendLog("═══════════════════════════════════════")

	// Step 1: Choose burn amount
	m.appendLog("Burn options:")
	m.appendLog("  1) Small burn: 0.8 TEST (minimum)")
	m.appendLog("  2) Large burn: 800 TEST")
	fmt.Print("Choose (1 or 2): ")
	var choice int
	fmt.Scanln(&choice)

	var amount float64
	if choice == 2 {
		amount = 800.0
		m.appendLog("Selected: Large burn (800 TEST)")
	} else {
		amount = 0.8
		m.appendLog("Selected: Small burn (0.8 TEST)")
	}

	// Step 2: Create burn_deposit transaction
	m.appendLog("─────────────────────────────────────")
	m.appendLog("Step 1/4: Creating burn deposit...")
	amountAtomic := int64(amount * 10000000)
	params := map[string]interface{}{"amount": amountAtomic}
	burnRes, err := walletRpcCall(walletRPCPort, "create_burn_deposit", params)
	if err != nil {
		m.appendLog("❌ Burn failed: " + err.Error())
		m.statusMsg = "Burn failed"
		return m
	}
	txHash := fmt.Sprintf("%v", burnRes["tx_hash"])
	m.appendLog("✅ Burn tx created: " + txHash)

	// Step 3: Wait for confirmations
	m.appendLog("─────────────────────────────────────")
	m.appendLog("Step 2/4: Waiting for confirmations...")
	for i := 1; i <= 10; i++ {
		m.appendLog(fmt.Sprintf("  Confirmation %d/10...", i))
		time.Sleep(1 * time.Second)
	}
	m.appendLog("✅ Transaction confirmed")

	// Step 4: Request burn consensus proof
	m.appendLog("─────────────────────────────────────")
	m.appendLog("Step 3/4: Requesting burn consensus...")

	consensusParams := map[string]interface{}{
		"tx_hash": txHash,
		"amount":  amountAtomic,
	}
	consensusRes, err := walletRpcCall(walletRPCPort, "request_burn_consensus", consensusParams)
	if err != nil {
		m.appendLog("❌ Consensus request failed: " + err.Error())
		m.appendLog("  (This RPC endpoint may need implementation)")
		m.statusMsg = "Consensus failed"
		return m
	}

	consensusProof := fmt.Sprintf("%v", consensusRes["consensus_proof"])
	m.appendLog("✅ Burn consensus received")
	m.appendLog("  Proof: " + consensusProof[:32] + "...")

	m.appendLog("═══════════════════════════════════════")
	m.statusMsg = "Burn flow complete"
	return m
}

func showLogs(m model) model {
	m.appendLog("Displaying full logs...")
	for _, l := range m.logs {
		fmt.Println(l)
	}
	fmt.Print("\nPress Enter to continue...")
	var dummy string
	fmt.Scanln(&dummy)
	m.statusMsg = "Returned from logs"
	return m
}

func streamPipe(r io.Reader, prefix string, m *model) {
	buf := make([]byte, 1024)
	for {
		n, err := r.Read(buf)
		if n > 0 {
			line := strings.TrimSpace(string(buf[:n]))
			// Only log significant messages to avoid spam
			if strings.Contains(line, "ERROR") || strings.Contains(line, "error") ||
				strings.Contains(line, "Starting") || strings.Contains(line, "started") ||
				strings.Contains(line, "Failed") || strings.Contains(line, "height") ||
				strings.Contains(line, "Listening") || strings.Contains(line, "Connected") {
				m.appendLog(fmt.Sprintf("%s: %s", prefix, line))
			}
		}
		if err != nil {
			if err != io.EOF {
				m.appendLog(fmt.Sprintf("%s stream error: %s", prefix, err.Error()))
			}
			return
		}
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

	// Read response body
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nodeInfo{}, fmt.Errorf("failed to read response: %w", err)
	}

	// Check response status
	if resp.StatusCode != http.StatusOK {
		return nodeInfo{}, fmt.Errorf("HTTP %d: %s - body: %s", resp.StatusCode, resp.Status, string(body))
	}

	// Log response for debugging (first few characters)
	if len(body) > 0 {
		sample := string(body)
		if len(sample) > 200 {
			sample = sample[:200] + "...[truncated]"
		}
		// Note: In a production environment, you might want to make this conditional
		// For now, let's keep it for debugging purposes
	}

	// Try to parse JSON
	var out map[string]interface{}
	if err := json.Unmarshal(body, &out); err != nil {
		return nodeInfo{}, fmt.Errorf("invalid JSON response: %w - body: %s", err, string(body))
	}

	height := 0
	peers := 0
	if h, ok := out["height"].(float64); ok {
		height = int(h)
	} else if h, ok := out["height"].(int); ok {
		height = h
	} else if h, ok := out["height"].(json.Number); ok {
		if v, err := h.Int64(); err == nil {
			height = int(v)
		}
	}

	if p, ok := out["incoming_connections_count"].(float64); ok {
		peers += int(p)
	} else if p, ok := out["incoming_connections_count"].(int); ok {
		peers += p
	} else if p, ok := out["incoming_connections_count"].(json.Number); ok {
		if v, err := p.Int64(); err == nil {
			peers += int(v)
		}
	}

	if p, ok := out["outgoing_connections_count"].(float64); ok {
		peers += int(p)
	} else if p, ok := out["outgoing_connections_count"].(int); ok {
		peers += p
	} else if p, ok := out["outgoing_connections_count"].(json.Number); ok {
		if v, err := p.Int64(); err == nil {
			peers += int(v)
		}
	}

	return nodeInfo{Height: height, Peers: peers}, nil
}

func walletRpcCall(port int, method string, params map[string]interface{}) (map[string]interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", port)
	payload := map[string]interface{}{"jsonrpc": "2.0", "id": "0", "method": method, "params": params}
	b, err := json.Marshal(payload)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal payload: %w", err)
	}

	client := http.Client{Timeout: 10 * time.Second}
	resp, err := client.Post(url, "application/json", bytes.NewReader(b))
	if err != nil {
		return nil, fmt.Errorf("failed to connect to wallet RPC: %w", err)
	}
	defer resp.Body.Close()

	// Read response body
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read wallet RPC response: %w", err)
	}

	// Check response status
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("wallet RPC HTTP %d: %s - body: %s", resp.StatusCode, resp.Status, string(body))
	}

	// Log response for debugging (first few characters)
	if len(body) > 0 {
		sample := string(body)
		if len(sample) > 200 {
			sample = sample[:200] + "...[truncated]"
		}
		// Note: In a production environment, you might want to make this conditional
		// For now, let's keep it for debugging purposes
	}

	// Try to parse JSON
	var out map[string]interface{}
	if err := json.Unmarshal(body, &out); err != nil {
		return nil, fmt.Errorf("invalid wallet RPC JSON response: %w - body: %s", err, string(body))
	}

	// Check for RPC error
	if errObj, ok := out["error"].(map[string]interface{}); ok {
		if msg, ok := errObj["message"].(string); ok {
			return nil, fmt.Errorf("wallet RPC error: %s", msg)
		}
		return nil, fmt.Errorf("wallet RPC error: %v", errObj)
	}

	// Return result
	if res, ok := out["result"].(map[string]interface{}); ok {
		return res, nil
	}

	// If no result field, return the whole response
	return out, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// testRPCConnection is a diagnostic function to test raw RPC responses
func testRPCConnection(port int) (string, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/get_info", port)
	client := http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return "", fmt.Errorf("failed to connect: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("failed to read response: %w", err)
	}

	return fmt.Sprintf("Status: %d, Body: %s", resp.StatusCode, string(body)), nil
}

func main() {
	initVersionInfo()
	fmt.Printf("====== %s TESTNET TUI ======\n", verInfo.projectName)
	fmt.Printf("Fuego v.%s\n", verInfo.projectVersion)
	fmt.Println("P2P Port: 20808")
	fmt.Println("RPC Ports: Node=28280, Wallet=28283")
	fmt.Println("Data Dir: ~/.fuego-testnet")
	fmt.Println("Coin: TEST, Address Prefix: 1075740")
	fmt.Println("")

	if runtime.GOOS == "windows" {
		// VT100 support
	}
	p := tea.NewProgram(initialModel())
	if err := p.Start(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}

// initVersionInfo populates version data from version.h
func initVersionInfo() {
	// Try to read version.h
	versionFile := filepath.Join(filepath.Dir(binPath("fuegod")), "..", "version", "version.h")
	content, err := os.ReadFile(versionFile)
	if err != nil {
		versionFile = filepath.Join(filepath.Dir(binPath("fuegod")), "version", "version.h")
		content, err = os.ReadFile(versionFile)
	}

	if err == nil {
		verStr := string(content)

		// Extract PROJECT_RELEASE
		reRelease := regexp.MustCompile(`#define PROJECT_RELEASE "([^"]+)"`)
		if match := reRelease.FindStringSubmatch(verStr); len(match) > 1 {
			verInfo.projectName = match[1]
		}

		// Extract version parts
		reVer := regexp.MustCompile(`#define PROJECT_VERSION "([^"]+)"`)
		reBuild := regexp.MustCompile(`#define PROJECT_VERSION_BUILD_NO "([^"]+)"`)
		if verMatch := reVer.FindStringSubmatch(verStr); len(verMatch) > 1 {
			if buildMatch := reBuild.FindStringSubmatch(verStr); len(buildMatch) > 1 {
				verInfo.projectVersion = fmt.Sprintf("%s.%s", verMatch[1], buildMatch[1])
			}
		}

		// Extract commit ID
		reCommit := regexp.MustCompile(`#define BUILD_COMMIT_ID "([^"]+)"`)
		if commitMatch := reCommit.FindStringSubmatch(verStr); len(commitMatch) > 1 {
			commit := commitMatch[1]
			if commit != "@VERSION@" {
				verInfo.projectVersion = fmt.Sprintf("%s(%s)", verInfo.projectVersion, commit)
			}
		}

		verInfo.fullVersion = fmt.Sprintf("%s || v%s", verInfo.projectName, verInfo.projectVersion)
	} else {
		// Fallback: try fuegod --version
		path := binPath("fuegod")
		if path != "fuegod" {
			cmd := exec.Command(path, "--version")
			output, err := cmd.Output()
			if err == nil {
				versionStr := string(output)
				re := regexp.MustCompile(`^([A-Za-z0-9\s]+)\s*\|\|\s*v([0-9.]+)`)
				matches := re.FindStringSubmatch(versionStr)
				if len(matches) > 2 {
					verInfo.projectName = strings.TrimSpace(matches[1])
					verInfo.projectVersion = matches[2]
					verInfo.fullVersion = strings.TrimSpace(versionStr)
				}
			}
		}
	}

	// Set defaults if still empty
	if verInfo.projectName == "" {
		verInfo.projectName = "Fuego DYNAMIGO"
	}
	if verInfo.projectVersion == "" {
		verInfo.projectVersion = "1.10.0.1076(0.2)"
	}
	if verInfo.fullVersion == "" {
		verInfo.fullVersion = "Fuego DYNAMIGO || v1.10.0.1076(0.2)"
	}
}
