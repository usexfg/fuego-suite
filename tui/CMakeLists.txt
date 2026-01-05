# TUI (Terminal User Interface) for Fuego
# Go-based terminal UI for controlling fuegod and walletd

cmake_minimum_required(VERSION 3.16)

# Check if Go is available
find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(WARNING "Go executable not found. TUI will not be built.")
    return()
endif()

message(STATUS "Found Go: ${GO_EXECUTABLE}")

# Get absolute paths
get_filename_component(TUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(TUI_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)

# Function to check Go version
function(check_go_version)
    execute_process(
        COMMAND ${GO_EXECUTABLE} version
        OUTPUT_VARIABLE GO_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Go version: ${GO_VERSION_OUTPUT}")

    # Extract version number
    string(REGEX MATCH "go([0-9]+)\\.([0-9]+)" GO_VERSION ${GO_VERSION_OUTPUT})
    if(NOT GO_VERSION)
        message(FATAL_ERROR "Failed to parse Go version")
    endif()

    set(GO_MAJOR ${CMAKE_MATCH_1})
    set(GO_MINOR ${CMAKE_MATCH_2})

    # Require Go 1.18 or higher
    if(${GO_MAJOR} LESS 1 OR (${GO_MAJOR} EQUAL 1 AND ${GO_MINOR} LESS 18))
        message(FATAL_ERROR "Go 1.18 or higher is required. Found Go ${GO_MAJOR}.${GO_MINOR}")
    endif()
endfunction()

check_go_version()

# Tidy modules first
add_custom_command(
    OUTPUT "${TUI_SOURCE_DIR}/go.sum"
    COMMAND sh -c "cd \"${TUI_SOURCE_DIR}\" && ${GO_EXECUTABLE} mod tidy"
    WORKING_DIRECTORY "${TUI_SOURCE_DIR}"
    DEPENDS "${TUI_SOURCE_DIR}/main.go" "${TUI_SOURCE_DIR}/config.go" "${TUI_SOURCE_DIR}/go.mod"
    COMMENT "Tidying Go modules"
    VERBATIM
)

# Build mainnet TUI (fuego-tui) - detects from binary name
add_custom_command(
    OUTPUT "${TUI_BINARY_DIR}/fuego-tui"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUI_BINARY_DIR}"
    COMMAND sh -c "cd \"${TUI_SOURCE_DIR}\" && ${GO_EXECUTABLE} build -o \"${TUI_BINARY_DIR}/fuego-tui\" ."
    WORKING_DIRECTORY "${TUI_SOURCE_DIR}"
    DEPENDS "${TUI_SOURCE_DIR}/main.go" "${TUI_SOURCE_DIR}/config.go" "${TUI_SOURCE_DIR}/go.mod" "${TUI_SOURCE_DIR}/go.sum"
    COMMENT "Building mainnet TUI (fuego-tui)"
    VERBATIM
)

# Build testnet TUI (testnet-tui) - uses testnet config
add_custom_command(
    OUTPUT "${TUI_BINARY_DIR}/testnet-tui"
    COMMAND sh -c "cd \"${TUI_SOURCE_DIR}\" && cp main.go main.go.bak && sed -i 's/CurrentConfig = MainnetConfig/CurrentConfig = TestnetConfig/' main.go"
    COMMAND sh -c "cd \"${TUI_SOURCE_DIR}\" && ${GO_EXECUTABLE} build -o \"${TUI_BINARY_DIR}/testnet-tui\" ."
    COMMAND sh -c "cd \"${TUI_SOURCE_DIR}\" && mv main.go.bak main.go"
    WORKING_DIRECTORY "${TUI_SOURCE_DIR}"
    DEPENDS "${TUI_SOURCE_DIR}/main.go" "${TUI_SOURCE_DIR}/config.go" "${TUI_SOURCE_DIR}/go.mod" "${TUI_SOURCE_DIR}/go.sum"
    COMMENT "Building testnet TUI (testnet-tui)"
    VERBATIM
)

# Custom targets
add_custom_target(fuego-tui ALL
    DEPENDS "${TUI_BINARY_DIR}/fuego-tui" "${TUI_BINARY_DIR}/go.sum"
    COMMENT "Mainnet TUI built"
)

add_custom_target(testnet-tui ALL
    DEPENDS "${TUI_BINARY_DIR}/testnet-tui" "${TUI_BINARY_DIR}/go.sum"
    COMMENT "Testnet TUI built"
)

# Install both
install(PROGRAMS "${TUI_BINARY_DIR}/fuego-tui" "${TUI_BINARY_DIR}/testnet-tui"
    DESTINATION bin
    COMPONENT Runtime
)

# Add TUI to the list of components
set(FUEGO_COMPONENTS ${FUEGO_COMPONENTS} TUI PARENT_SCOPE)

# Documentation
install(FILES "${TUI_SOURCE_DIR}/README.md"
    DESTINATION share/doc/fuego/tui
    COMPONENT Development
    OPTIONAL
)
```

<file_path>
fuego/tui/main.go
</file_path>

<edit_description>
Complete tview-based main.go with tview, forms, modals, mouse support
</edit_description>

```go
package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/rivo/tview"
)

type Tui struct {
	app         *tview.Application
	pages       *tview.Pages
	logs        []string
	nodeCmd     *exec.Cmd
	walletCmd   *exec.Cmd
	runningNode bool
	runningW    bool
	height      int
	peers       int
	version     string
}

func main() {
	// Detect mode from executable name
	executable := filepath.Base(os.Args[0])
	if strings.Contains(strings.ToLower(executable), "testnet") {
		CurrentConfig = TestnetConfig
	} else {
		CurrentConfig = MainnetConfig
	}

	version := getVersion()
	tui := &Tui{
		app:     tview.NewApplication(),
		logs:    []string{fmt.Sprintf("🔥 %s %s TUI Ready", CurrentConfig.CoinName, CurrentConfig.NetworkName)},
		version: version,
	}

	tui.app.EnableMouse(true)
	tui.showMainMenu()

	if err := tui.app.Run(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}

func getVersion() string {
	versionFile := filepath.Join("/home/ar/fuego", "build", "release", "version", "version.h")
	content, err := os.ReadFile(versionFile)
	if err != nil {
		return "DEV"
	}
	re := regexp.MustCompile(`#define PROJECT_VERSION "([^"]+)"`)
	match := re.FindStringSubmatch(string(content))
	if len(match) > 1 {
		return match[1]
	}
	return "DEV"
}

func (t *Tui) showMainMenu() {
	header := tview.NewTextView().
		SetTextAlign(tview.AlignCenter).
		SetDynamicColors(true).
		SetText(fmt.Sprintf("[yellow]🔥 %s %s TUI\n[green]%s\n[grey]Version: %s | Ports:%d/%d",
			CurrentConfig.CoinName, CurrentConfig.NetworkName, time.Now().Format("2006-01-02 15:04:05"), t.version, CurrentConfig.NodeRPCPort, CurrentConfig.WalletRPCPort))

	list := tview.NewList().ShowSecondaryText(true)
	list.SetDoneFunc(func() { t.app.Stop() })

	list.AddItem("Node & Wallet", "Start/stop/status", '1', func() { t.showNodeWalletMenu() })
	list.AddItem("Elderfier", "Staking & voting", '2', func() { t.showElderfierMenu() })
	list.AddItem("Æternal Flame", "Burn2Mint", '3', func() { t.showBurn2MintMenu() })
	list.AddItem("Transfer", "Wallet & TX", '4', func() { t.showTransferMenu() })
	list.AddItem("All Logs", "View history", 'l', t.showLogs)
	list.AddItem("[red]Quit", "Exit TUI", 'q', func() { t.app.Stop() })

	status := tview.NewTextView().SetDynamicColors(true).SetTextFunc(func() string {
		s := fmt.Sprintf("[yellow]%s:[white] ", CurrentConfig.NetworkName)
		s += "[green]Running\n"
		if t.runningNode {
			s += fmt.Sprintf("Node [green]✓ [white]H:%d P:%d\n", t.height, t.peers)
		} else {
			s += "Node [red]✗\n"
		}
		if t.runningW {
			s += fmt.Sprintf("Wallet [green]✓ [white]Port:%d\n", CurrentConfig.WalletRPCPort)
		} else {
			s += "Wallet [red]✗\n"
		}
		return s
	})

	logView := tview.NewTextView()
	logView.SetDynamicColors(true)
	logView.SetScrollable(true)
	logView.SetChangedFunc(func() { t.app.Draw() })
	t.updateLogs(logView)

	flex := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(header, 3, 1, false).
		AddItem(tview.NewFlex().
			AddItem(list, 0, 3, true).
			AddItem(logView, 0, 4, false), 0, 1, true).
		AddItem(status, 4, 1, false)

	t.pages = tview.NewPages()
	t.pages.AddPage("main", flex, true, true)
	t.app.SetRoot(t.pages, true)
}

func (t *Tui) addLogPage(title string, list *tview.List) {
	logView := tview.NewTextView().SetDynamicColors(true).SetScrollable(true).SetChangedFunc(func() { t.app.Draw() })
	t.updateLogs(logView)
	list.SetDoneFunc(func() { t.pages.RemovePage(title) })

	flex := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(tview.NewFlex().
			AddItem(list, 0, 2, true).
			AddItem(logView, 0, 3, false), 0, 1, true)

	t.pages.AddPage(title, flex, true, true)
}

// Sub-menus
func (t *Tui) showNodeWalletMenu() {
	list := tview.NewList().
		SetTitle(fmt.Sprintf("[yellow]%s Node/Wallet", CurrentConfig.CoinName)).
		SetBorder(true)

	list.AddItem("Start Node", fmt.Sprintf("%s on %d/%d", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort, CurrentConfig.NodeP2PPort), '1', t.startNode)
	list.AddItem("Stop Node", "Kill process", '2', t.stopNode)
	list.AddItem("Node Status", "Height/peers", '3', t.nodeStatus)
	list.AddItem("Start Wallet", fmt.Sprintf("%s on %d", CurrentConfig.WalletBinary, CurrentConfig.WalletRPCPort), '4', t.startWallet)
	list.AddItem("Stop Wallet", "Kill process", '5', t.stopWallet)
	list.AddItem("Stop All", "Everything", '0', func() { t.stopNode(); t.stopWallet() })
	t.addLogPage("nodeload", list)
}

func (t *Tui) showElderfierMenu() {
	list := tview.NewList().
		SetTitle("[cyan]Elderfier Menu").
		SetBorder(true)

	stake := float64(CurrentConfig.StakeAmount) / float64(CurrentConfig.CoinUnits)
	list.AddItem("Start Stayking", fmt.Sprintf("Stake %.0f %s", stake, CurrentConfig.CoinName), '1', t.startStake)
	list.AddItem("Check Status", "Get stake info", '2', t.checkStake)
	list.AddItem("View Consensus", "Pending items", '3', func() { t.rpcCall("consensus", CurrentConfig.GetConsensusRPC, nil) })
	list.AddItem("Vote on Pending", "get_pending_votes", '4', func() { t.rpcCall("pending_votes", CurrentConfig.GetPendingVotesRPC, nil) })
	list.AddItem("Register ENindex", "8-char ID", '5', t.registerENindex)
	list.AddItem("Increase Stake", "Add more", '6', t.increaseStake)
	list.AddItem("Update ENindex", "Change ID", '7', t.updateENindex)
	t.addLogPage("elder", list)
}

func (t *Tui) showBurn2MintMenu() {
	list := tview.NewList().
		SetTitle("[orange]Æternal Flame").
		SetBorder(true)

	small := float64(CurrentConfig.BurnSmallAmount) / float64(CurrentConfig.CoinUnits)
	large := float64(CurrentConfig.BurnLargeAmount) / float64(CurrentConfig.CoinUnits)

	list.AddItem("Small Burn", fmt.Sprintf("%.1f %s", small, CurrentConfig.CoinName), '1', func() { t.burnModal(small) })
	list.AddItem("Large Burn", fmt.Sprintf("%.0f %s", large, CurrentConfig.CoinName), '2', func() { t.burnModal(large) })
	list.AddItem("Custom Burn", "Enter amount", '3', t.burnCustom)
	list.AddItem("Get Consensus", "Eldernode proof", '4', t.requestConsensus)
	list.AddItem("Generate STARK", "xfg-stark CLI", '5', t.generateStark)
	t.addLogPage("burn", list)
}

func (t *Tui) showTransferMenu() {
	list := tview.NewList().
		SetTitle("[green]Transfer Menu").
		SetBorder(true)

	list.AddItem("Create Wallet", "New address", '1', t.createWallet)
	list.AddItem("Get Balance", "Query", '2', t.getBalance)
	list.AddItem("Get Address", "Show address", '3', t.getAddress)
	list.AddItem("Send TX", "Transfer funds", '4', t.sendTxForm)
	list.AddItem("View Address", "Full details", '5', t.viewAddress)
	t.addLogPage("transfer", list)
}

// Logging
func (t *Tui) log(msg string, args ...interface{}) {
	now := time.Now().Format("15:04:05")
	t.logs = append(t.logs, fmt.Sprintf("%s %s", now, fmt.Sprintf(msg, args...)))
	if len(t.logs) > 200 {
		t.logs = t.logs[len(t.logs)-200:]
	}
}

func (t *Tui) updateLogs(view *tview.TextView) {
	view.Clear()
	start := len(t.logs) - 50
	if start < 0 { start = 0 }
	for _, line := range t.logs[start:] {
		fmt.Fprintf(view, "%s\n", line)
	}
}

func (t *Tui) showLogs() {
	logView := tview.NewTextView().SetScrollable(true).SetDynamicColors(true).SetTitle(" Full Log ").SetBorder(true)
	for _, line := range t.logs {
		fmt.Fprintf(logView, "%s\n", line)
	}

	closeBtn := tview.NewButton(" Close (Esc) ").SetSelectedFunc(func() {
		t.pages.RemovePage("logsmodal")
	})

	flex := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(logView, 0, 1, true).
		AddItem(closeBtn, 1, 1, false)

	t.pages.AddPage("logsmodal", flex, true, true)
}

// Binary path
func (t *Tui) binPath(binary string) string {
	if strings.HasPrefix(os.Getenv("HOME"), "/home/ar") {
		// Check parent build directory
		devPath := filepath.Join("/home/ar/fuego", "build", "release", "src", binary)
		if _, err := os.Stat(devPath); err == nil {
			return devPath
		}
	}
	// Check local build
	cwd, _ := os.Getwd()
	devPath := filepath.Join(cwd, "build", "release", "src", binary)
	if _, err := os.Stat(devPath); err == nil {
		return devPath
	}
	devPath = filepath.Join(cwd, "build", "src", binary)
	if _, err := os.Stat(devPath); err == nil {
		return devPath
	}
	return binary
}

// Node functions
func (t *Tui) startNode() {
	if t.runningNode {
		t.log("Node already running")
		return
	}

	path := t.binPath(CurrentConfig.NodeBinary)
	if path == CurrentConfig.NodeBinary {
		t.log("ERROR: %s not found", CurrentConfig.NodeBinary)
		return
	}

	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)

	args := []string{
		"--data-dir", dataDir,
		"--rpc-bind-port", fmt.Sprintf("%d", CurrentConfig.NodeRPCPort),
		"--p2p-bind-port", fmt.Sprintf("%d", CurrentConfig.NodeP2PPort),
	}

	t.log("Starting %s %s", CurrentConfig.NodeBinary, strings.Join(args, " "))
	cmd := exec.Command(path, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()

	if err := cmd.Start(); err != nil {
		t.log("ERROR: Start failed: %s", err)
		return
	}

	t.nodeCmd = cmd
	t.runningNode = true
	t.log("✓ Started %s on %d/%d", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort, CurrentConfig.NodeP2PPort)

	go t.streamPipe(stdout, "NODE")
	go t.streamPipe(stderr, "NODE-ERR")

	go func() {
		time.Sleep(3 * time.Second)
		for t.runningNode {
			t.updateNodeInfo()
			time.Sleep(5 * time.Second)
		}
	}()
}

func (t *Tui) stopNode() {
	if !t.runningNode || t.nodeCmd == nil {
		t.log("Node not running")
		return
	}
	_ = t.nodeCmd.Process.Kill()
	t.nodeCmd = nil
	t.runningNode = false
	t.log("Node stopped")
}

func (t *Tui) stopWallet() {
	if !t.runningW || t.walletCmd == nil {
		t.log("Wallet not running")
		return
	}
	_ = t.walletCmd.Process.Kill()
	t.walletCmd = nil
	t.runningW = false
	t.log("Wallet stopped")
}

func (t *Tui) streamPipe(r interface{ Read([]byte) (int, error) }, prefix string) {
	buf := make([]byte, 1024)
	for {
		n, err := r.Read(buf)
		if err != nil {
			break
		}
		if n > 0 {
			t.log("%s: %s", prefix, strings.TrimSpace(string(buf[:n])))
		}
	}
}

func (t *Tui) updateNodeInfo() {
	url := fmt.Sprintf("http://127.0.0.1:%d/get_info", CurrentConfig.NodeRPCPort)
	client := &http.Client{Timeout: 3 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		t.log("Node query failed: %s", err)
		return
	}
	defer resp.Body.Close()

	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err == nil {
		if h, ok := out["height"].(float64); ok {
			t.height = int(h)
		}
		peerCount := 0
		if p, ok := out["incoming_connections_count"].(float64); ok {
			peerCount += int(p)
		}
		if p, ok := out["outgoing_connections_count"].(float64); ok {
			peerCount += int(p)
		}
		t.peers = peerCount
		t.log("Height:%d Peers:%d", t.height, t.peers)
	}
}

func (t *Tui) nodeStatus() {
	t.updateNodeInfo()
}

func (t *Tui) startWallet() {
	if t.runningW {
		t.log("Wallet already running")
		return
	}

	path := t.binPath(CurrentConfig.WalletBinary)
	if path == CurrentConfig.WalletBinary {
		t.log("ERROR: %s not found", CurrentConfig.WalletBinary)
		return
	}

	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)

	args := []string{
		"--daemon-port", fmt.Sprintf("%d", CurrentConfig.NodeRPCPort),
		"--bind-address", "127.0.0.1",
		"--bind-port", fmt.Sprintf("%d", CurrentConfig.WalletRPCPort),
		"--data-dir", dataDir,
	}

	// Add --testnet for testnet builds
	if CurrentConfig.NetworkName == "TESTNET" {
		args = append(args, "--testnet")
		t.log("Adding --testnet flag")
	}

	t.log("Starting %s %s", CurrentConfig.WalletBinary, strings.Join(args, " "))
	cmd := exec.Command(path, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()

	if err := cmd.Start(); err != nil {
		t.log("ERROR: Wallet failed: %s", err)
		return
	}

	t.walletCmd = cmd
	t.runningW = true
	t.log("✓ Started %s on %d", CurrentConfig.WalletBinary, CurrentConfig.WalletRPCPort)

	go t.streamPipe(stdout, "WALLET")
	go t.streamPipe(stderr, "WALLET-ERR")
}

// Wallet RPC
func (t *Tui) rpcCall(name string, method string, params map[string]interface{}) {
	t.log("RPC %s...", name)
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", CurrentConfig.WalletRPCPort)
	payload := map[string]interface{}{"jsonrpc": "2.0", "id": "0", "method": method, "params": params}
	b, _ := json.Marshal(payload)
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Post(url, "application/json", strings.NewReader(string(b)))
	if err != nil {
		t.log("ERROR: %s: %s", name, err)
		return
	}
	defer resp.Body.Close()
	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err == nil {
		if res, ok := out["result"].(map[string]interface{}); ok {
			t.log("✓ %s: %v", name, res)
		} else {
			t.log("✓ %s: %v", name, out)
		}
	}
}

func (t *Tui) rpcSimple(method string, params map[string]interface{}) (interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", CurrentConfig.WalletRPCPort)
	payload := map[string]interface{}{"jsonrpc": "2.0", "id": "0", "method": method, "params": params}
	b, _ := json.Marshal(payload)
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Post(url, "application/json", strings.NewReader(string(b)))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	if res, ok := out["result"]; ok {
		return res, nil
	}
	return out, nil
}

func (t *Tui) createWallet() {
	t.rpcCall("create_address", CurrentConfig.CreateAddressRPC, nil)
}

func (t *Tui) getBalance() {
	t.rpcCall("get_balance", CurrentConfig.GetBalanceRPC, nil)
}

func (t *Tui) getAddress() {
	t.rpcCall("get_address", CurrentConfig.GetAddressesRPC, nil)
}

func (t *Tui) startStake() {
	stake := float64(CurrentConfig.StakeAmount) / float64(CurrentConfig.CoinUnits)
	t.log("Creating stake %.0f %s...", stake, CurrentConfig.CoinName)
	params := map[string]interface{}{
		"amount": CurrentConfig.StakeAmount,
		"type":   "elderfier_stake",
	}
	t.rpcCall("stake", CurrentConfig.CreateStakeRPC, params)
}

func (t *Tui) checkStake() {
	t.rpcCall("get_stake", CurrentConfig.GetStakeStatusRPC, nil)
}

func (t *Tui) registerENindex() {
	form := tview.NewForm().
		SetTitle("[cyan]Register to ENindex").
		SetBorder(true)

	var elderID, txHash string
	form.AddInputField("Elderfier ID (8 chars)", "", 8, nil, func(text string) {
		elderID = text
	})
	form.AddInputField("Stake TX Hash", "", 64, nil, func(text string) {
		txHash = text
	})
	form.AddButton("Register", func() {
		if len(elderID) != 8 {
			t.log("ERROR: Need 8-char ID")
			return
		}
		addrRes, err := t.rpcSimple(CurrentConfig.GetAddressesRPC, nil)
		if err != nil {
			t.log("ERROR: %s", err)
			return
		}
		params := map[string]interface{}{
			"elder_id":      elderID,
			"stake_tx_hash": txHash,
			"address":       fmt.Sprintf("%v", addrRes),
		}
		t.rpcCall("register", CurrentConfig.RegisterEnindexRPC, params)
		t.pages.RemovePage("enindex")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("enindex") })
	t.pages.AddPage("enindex", form, true, true)
}

func (t *Tui) increaseStake() {
	form := tview.NewForm().
		SetTitle("[cyan]Increase Stake").
		SetBorder(true)

	var amount int64
	form.AddInputField("Amount (atomic)", fmt.Sprintf("%d", CurrentConfig.TestTxAmount), 15, nil, func(text string) {
		fmt.Sscanf(text, "%d", &amount)
	})
	form.AddButton("Increase", func() {
		params := map[string]interface{}{"amount": amount}
		t.rpcCall("increase", CurrentConfig.IncreaseStakeRPC, params)
		t.pages.RemovePage("increase")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("increase") })
	t.pages.AddPage("increase", form, true, true)
}

func (t *Tui) updateENindex() {
	form := tview.NewForm().
		SetTitle("[cyan]Update ENindex").
		SetBorder(true)

	var newID string
	form.AddInputField("New Elder ID", "", 8, nil, func(text string) {
		newID = text
	})
	form.AddButton("Update", func() {
		params := map[string]interface{}{}
		if len(newID) == 8 {
			params["elder_id"] = newID
		}
		t.rpcCall("update", CurrentConfig.UpdateEnindexRPC, params)
		t.pages.RemovePage("update")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("update") })
	t.pages.AddPage("update", form, true, true)
}

func (t *Tui) burnModal(amount float64) {
	t.log("Burn %.2f %s requested", amount, CurrentConfig.CoinName)
	modal := tview.NewModal().
		SetText(fmt.Sprintf("Burn %.2f %s?", amount, CurrentConfig.CoinName)).
		AddButtons([]string{"Burn", "Cancel"}).
		SetDoneFunc(func(i int, label string) {
			if label == "Burn" {
				params := map[string]interface{}{"amount": int64(amount * float64(CurrentConfig.CoinUnits))}
				t.rpcCall("burn", CurrentConfig.CreateBurnRPC, params)
			}
			t.pages.RemovePage("burnmodal")
		})
	t.pages.AddPage("burnmodal", modal, true, true)
}

func (t *Tui) burnCustom() {
	form := tview.NewForm().
		SetTitle("[orange]Custom Burn").
		SetBorder(true)

	var amount float64
	form.AddInputField("Amount", "1.0", 10, nil, func(text string) {
		fmt.Sscanf(text, "%f", &amount)
	})
	form.AddButton("Burn", func() {
		t.burnModal(amount)
		t.pages.RemovePage("customburn")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("customburn") })
	t.pages.AddPage("customburn", form, true, true)
}

func (t *Tui) requestConsensus() {
	form := tview.NewForm().
		SetTitle("[orange]Request Consensus").
		SetBorder(true)

	var txHash string
	var amount int64
	form.AddInputField("Burn TX Hash", "", 64, nil, func(text string) {
		txHash = text
	})
	form.AddInputField("Amount (atomic)", fmt.Sprintf("%d", CurrentConfig.BurnSmallAmount), 15, nil, func(text string) {
		fmt.Sscanf(text, "%d", &amount)
	})
	form.AddButton("Request", func() {
		params := map[string]interface{}{"tx_hash": txHash, "amount": amount}
		t.rpcCall("consensus", CurrentConfig.RequestConsensusRPC, params)
		t.pages.RemovePage("consensus")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("consensus") })
	t.pages.AddPage("consensus", form, true, true)
}

func (t *Tui) generateStark() {
	form := tview.NewForm().
		SetTitle("[orange]Generate STARK").
		SetBorder(true)

	var txHash, eldernodeProof string
	var amount int64
	form.AddInputField("Burn TX Hash", "", 64, nil, func(text string) {
		txHash = text
	})
	form.AddInputField("Amount", fmt.Sprintf("%d", CurrentConfig.BurnSmallAmount), 15, nil, func(text string) {
		fmt.Sscanf(text, "%d", &amount)
	})
	form.AddInputField("Eldernode Proof", "", 100, nil, func(text string) {
		eldernodeProof = text
	})
	form.AddButton("Generate", func() {
		starkPath := t.binPath("xfg-stark")
		if starkPath == "xfg-stark" {
			t.log("ERROR: xfg-stark not found")
			return
		}
		cmd := exec.Command(starkPath, "generate-proof",
			"--tx-hash", txHash,
			"--amount", fmt.Sprintf("%d", amount),
			"--eldernode-proof", eldernodeProof,
		)
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.log("ERROR: %s", err)
			t.log("Output: %s", string(out))
		} else {
			t.log("✓ STARK generated")
		}
		t.pages.RemovePage("stark")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("stark") })
	t.pages.AddPage("stark", form, true, true)
}

func (t *Tui) sendTxForm() {
	form := tview.NewForm().
		SetTitle("[green]Send TX").
		SetBorder(true)

	var recipient string
	var amount float64
	form.AddInputField("Recipient", "", 64, nil, func(text string) {
		recipient = text
	})
	form.AddInputField(fmt.Sprintf("Amount (%s)", CurrentConfig.CoinName), "1.0", 10, nil, func(text string) {
		fmt.Sscanf(text, "%f", &amount)
	})
	form.AddButton("Send", func() {
		if recipient == "" {
			t.log("ERROR: Recipient required")
			return
		}
		t.log("Sending %.2f %s...", amount, CurrentConfig.CoinName)
		params := map[string]interface{}{
			"transfers": []map[string]interface{}{{
				"address": recipient,
				"amount":  int64(amount * float64(CurrentConfig.CoinUnits)),
			}},
		}
		t.rpcCall("send", CurrentConfig.SendTransactionRPC, params)
		t.pages.RemovePage("sendtx")
	})
	form.AddButton("Cancel", func() { t.pages.RemovePage("sendtx") })
	t.pages.AddPage("sendtx", form, true, true)
}

func (t *Tui) viewAddress() {
	addrRes, err := t.rpcSimple(CurrentConfig.GetAddressesRPC, nil)
	if err != nil {
		t.log("ERROR: No wallet")
		return
	}
	addr := fmt.Sprintf("%v", addrRes)

	form := tview.NewForm().
		SetTitle("[green]Wallet Address").
		SetBorder(true)
	form.AddTextView("Address", addr, 64, 1, true)
	form.AddButton("Close", func() { t.pages.RemovePage("viewaddr") })
	t.pages.AddPage("viewaddr", form, true, true)
}
```

<file_path> f
