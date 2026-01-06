module github.com/usexfg/fuego-suite/tui-testnet

go 1.20

require (
	github.com/rivo/tview v0.0.0-20241227224719-2d18d57762c3
)

require (
	github.com/gdamore/encoding v1.0.0 // indirect
	github.com/gdamore/tcell/v2 v2.7.4 // indirect
	github.com/lucasb-eyer/go-colorful v1.2.0 // indirect
	github.com/mattn/go-runewidth v0.0.16 // indirect
	github.com/rivo/uniseg v0.4.7 // indirect
	golang.org/x/sys v0.28.0 // indirect
	golang.org/x/term v0.27.0 // indirect
	golang.org/x/text v0.21.0 // indirect
)
```

<file_path>
fuego/tui-testnet/main.go
</file_path>

<edit_description>
Complete tview-based Fuego Testnet TUI with sections, modals, forms, and mouse support
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

const (
	nodeRPCPort   = 28280
	walletRPCPort = 28283
	nodeP2PPort   = 20808
)

var verInfo = struct {
	projectName    string
	projectVersion string
	testnetVersion string
	fullVersion    string
}{
	projectName: "Fuego DYNAMIGO",
}

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
}

func main() {
	initVersionInfo()

	tui := &Tui{
		app:  tview.NewApplication(),
		logs: []string{"🔥 FUEGO Testnet TUI Ready - " + verInfo.fullVersion},
	}

	tui.app.EnableMouse(true)
	tui.setupUI()

	if err := tui.app.Run(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}

func initVersionInfo() {
	versionFile := filepath.Join("/home/ar/fuego", "build", "release", "version", "version.h")
	content, err := os.ReadFile(versionFile)
	if err == nil {
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

func (t *Tui) setupUI() {
	t.pages = tview.NewPages()
	t.showMainMenu()
	t.app.SetRoot(t.pages, true)
}

func (t *Tui) showMainMenu() {
	header := tview.NewTextView().
		SetTextAlign(tview.AlignCenter).
		SetDynamicColors(true).
		SetText(fmt.Sprintf("[yellow]🔥 %s TESTNET TUI\n[green]%s\n[grey]Mouse enabled - Click items or use shortcuts", verInfo.projectName, verInfo.fullVersion))

	list := tview.NewList().
		ShowSecondaryText(true).
		SetDoneFunc(func() {
			t.app.Stop()
		})

	list.AddItem("Node & Wallet", "Start/Stop node, wallet RPC, status", '1', func() {
		t.showNodeWalletMenu()
	})
	list.AddItem("Elderfier Menu", "Staking, consensus, voting", '2', func() {
		t.showElderfierMenu()
	})
	list.AddItem("Æternal Flame", "Burn2Mint flow (XFG → HEAT)", '3', func() {
		t.showBurn2MintMenu()
	})
	list.AddItem("Transfer", "Create wallet, send, receive", '4', func() {
		t.showTransferMenu()
	})
	list.AddItem("View All Logs", "Show complete log history", 'l', func() {
		t.showLogs()
	})
	list.AddItem("[red]Quit", "Exit the TUI", 'q', func() {
		t.app.Stop()
	})

	status := tview.NewTextView().
		SetDynamicColors(true).
		SetTextFunc(func() string {
			text := fmt.Sprintf("[white]Status: [yellow]%s\n", t.getStatus())
			if t.runningNode {
				text += fmt.Sprintf("[white]Node: [green]running[white] | Height: [cyan]%d[white] | Peers: [cyan]%d\n", t.height, t.peers)
			} else {
				text += "[white]Node: [red]stopped\n"
			}
			if t.runningW {
				text += fmt.Sprintf("[white]Wallet: [green]running[white] on port [cyan]%d\n", walletRPCPort)
			} else {
				text += "[white]Wallet: [red]stopped\n"
			}
			return text
		})

	logView := tview.NewTextView().
		SetDynamicColors(true).
		SetScrollable(true).
		SetChangedFunc(func() {
			t.app.Draw()
		})

	t.updateLogs(logView)

	flex := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(header, 3, 1, false).
		AddItem(tview.NewFlex().
			AddItem(list, 0, 2, true).
			AddItem(logView, 0, 2, false), 0, 1, true).
		AddItem(status, 4, 1, false)

	t.pages.AddPage("main", flex, true, true)
}

func (t *Tui) showNodeWalletMenu() {
	list := tview.NewList().
		SetTitle("[yellow]Node & Wallet Controls").
		SetBorder(true)

	list.AddItem("Start Node", "Launch testnetd on ports 28280/28080", '1', t.startNode)
	list.AddItem("Stop Node", "Kill running testnetd process", '2', t.stopNode)
	list.AddItem("Node Status", "Query height and peer count", '3', t.nodeStatus)
	list.AddItem("Start Wallet RPC", "Launch walletd on port 28283", '4', t.startWallet)
	list.AddItem("[gray]Back", "Return to main menu", '0', func() {
		t.pages.RemovePage("nodeload")
	})

	t.addLogViewAndBack(list, "nodeload")
}

func (t *Tui) showElderfierMenu() {
	list := tview.NewList().
		SetTitle("[cyan]Elderfier Menu").
		SetBorder(true)

	list.AddItem("Start Elderfier Stayking", "Create 80 XFG stake deposit", '1', t.startStake)
	list.AddItem("Check Stake Status", "Query get_stake_status", '2', t.checkStake)
	list.AddItem("View Consensus Requests", "get_consensus_requests", '3', t.viewConsensus)
	list.AddItem("Vote on Pending", "get_pending_votes + vote", '4', t.voteOnPending)
	list.AddItem("Register to ENindex", "Interactive ENindex registration", '5', t.registerENindex)
	list.AddItem("Increase Stake", "Add to existing stake", '6', t.increaseStake)
	list.AddItem("Update ENindex Keys", "Change Elderfier ID", '7', t.updateENindex)
	list.AddItem("[gray]Back", "Return to main menu", '0', func() {
		t.pages.RemovePage("elder")
	})

	t.addLogViewAndBack(list, "elder")
}

func (t *Tui) showBurn2MintMenu() {
	list := tview.NewList().
		SetTitle("[orange]Æternal Flame Menu (Burn2Mint)").
		SetBorder(true)

	list.AddItem("Burn 0.8 XFG", "Small burn for testing", '1', func() {
		t.burnModal(0.8)
	})
	list.AddItem("Burn 800 XFG", "Large burn (HEAT mint)", '2', func() {
		t.burnModal(800.0)
	})
	list.AddItem("Burn Custom Amount", "Enter custom XFG amount", '3', t.burnCustom)
	list.AddItem("Request Elderfier Consensus", "Get eldernode proof", '4', t.requestConsensus)
	list.AddItem("Generate STARK Proof", "Requires xfg-stark CLI", '5', t.generateStark)
	list.AddItem("[gray]Back", "Return to main menu", '0', func() {
		t.pages.RemovePage("burn")
	})

	t.addLogViewAndBack(list, "burn")
}

func (t *Tui) showTransferMenu() {
	list := tview.NewList().
		SetTitle("[green]Transfer Menu").
		SetBorder(true)

	list.AddItem("Create New Wallet", "Generate new address", '1', t.createWallet)
	list.AddItem("Get Balance", "Query get_balance", '2', t.getBalance)
	list.AddItem("Get Address", "Show wallet address", '3', t.getAddress)
	list.AddItem("Send Transaction", "Interactive transfer form", '4', t.sendTxForm)
	list.AddItem("Receive (View Address)", "Show QR-ready address", '5', t.viewAddress)
	list.AddItem("[gray]Back", "Return to main menu", '0', func() {
		t.pages.RemovePage("transfer")
	})

	t.addLogViewAndBack(list, "transfer")
}

func (t *Tui) addLogViewAndBack(list *tview.List, pageName string) {
	logView := tview.NewTextView().
		SetDynamicColors(true).
		SetScrollable(true).
		SetChangedFunc(func() {
			t.app.Draw()
		})

	t.updateLogs(logView)

	flex := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(tview.NewFlex().
			AddItem(list, 0, 2, true).
			AddItem(logView, 0, 2, false), 0, 1, true)

	t.pages.AddPage(pageName, flex, true, true)
}

func (t *Tui) getStatus() string {
	if t.runningNode && t.runningW {
		return "[green]Both running"
	} else if t.runningNode {
		return "[green]Node running"
	} else if t.runningW {
		return "[green]Wallet running"
	}
	return "[yellow]Ready"
}

func (t *Tui) log(s string) {
	t.logs = append(t.logs, fmt.Sprintf("%s %s", time.Now().Format("15:04:05"), s))
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
	logView := tview.NewTextView().
		SetScrollable(true).
		SetDynamicColors(true).
		SetTitle("Full Log History").
		SetBorder(true)

	for _, line := range t.logs {
		fmt.Fprintf(logView, "%s\n", line)
	}

	flex := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(logView, 0, 1, true)

	closeBtn := tview.NewButton("Close (Esc)").SetSelectedFunc(func() {
		t.pages.RemovePage("logsmodal")
	})

	flex.AddItem(closeBtn, 1, 1, false)
	t.pages.AddPage("logsmodal", flex, true, true)
}

func (t *Tui) binPath(name string) string {
	binPath := filepath.Join("/home/ar/fuego", "build", "release", "src", name)
	if _, err := os.Stat(binPath); err == nil {
		return binPath
	}
	return name
}

func (t *Tui) startNode() {
	if t.runningNode {
		t.log("Node already running")
		return
	}

	path := t.binPath("testnetd")
	if path == "testnetd" {
		t.log("ERROR: testnetd binary not found")
		return
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
		t.log(fmt.Sprintf("ERROR: Failed to start node: %s", err))
		return
	}

	t.nodeCmd = cmd
	t.runningNode = true
	t.log(fmt.Sprintf("Started testnetd: %s", path))

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
	t.log("Stopped testnetd")
}

func (t *Tui) updateNodeInfo() {
	url := fmt.Sprintf("http://127.0.0.1:%d/get_info", nodeRPCPort)
	client := &http.Client{Timeout: 3 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		t.log(fmt.Sprintf("Node query failed: %s", err))
		return
	}
	defer resp.Body.Close()

	var out map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&out); err == nil {
		if h, ok := out["height"].(float64); ok {
			t.height = int(h)
		}
		peers := 0
		if p, ok := out["incoming_connections_count"].(float64); ok {
			peers += int(p)
		}
		if p, ok := out["outgoing_connections_count"].(float64); ok {
			peers += int(p)
		}
		t.peers = peers
		t.log(fmt.Sprintf("Height: %d, Peers: %d", t.height, t.peers))
	}
}

func (t *Tui) nodeStatus() {
	t.updateNodeInfo()
}

func (t *Tui) startWallet() {
	if t.runningW {
		t.log("Wallet RPC already running")
		return
	}

	path := t.binPath("walletd")
	if path == "walletd" {
		t.log("ERROR: walletd binary not found")
		return
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
		t.log(fmt.Sprintf("ERROR: Failed to start walletd: %s", err))
		return
	}

	t.walletCmd = cmd
	t.runningW = true
	t.log(fmt.Sprintf("Started walletd: %s", path))

	go t.streamPipe(stdout, "WALLET")
	go t.streamPipe(stderr, "WALLET-ERR")
}

func (t *Tui) streamPipe(r interface{ Read([]byte) (int, error) }, prefix string) {
	buf := make([]byte, 1024)
	for {
		n, err := r.Read(buf)
		if err != nil {
			break
		}
		if n > 0 {
			t.log(prefix + ": " + strings.TrimSpace(string(buf[:n])))
		}
	}
}

func (t *Tui) walletRpcCall(method string, params map[string]interface{}) (map[string]interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", walletRPCPort)
	payload := map[string]interface{}{"jsonrpc": "2.0", "id": "0", "method": method, "params": params}
	b, _ := json.Marshal(payload)
	client := &http.Client{Timeout: 4 * time.Second}
	resp, err := client.Post(url, "application/json", strings.NewReader(string(b)))
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

func (t *Tui) createWallet() {
	t.log("Creating wallet...")
	res, err := t.walletRpcCall("create_address", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Create wallet failed: %s", err))
		return
	}
	t.log(fmt.Sprintf("Wallet created: %v", res))
}

func (t *Tui) getBalance() {
	t.log("Querying balance...")
	res, err := t.walletRpcCall("get_balance", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Get balance failed: %s", err))
		return
	}
	t.log(fmt.Sprintf("Balance: %v", res))
}

func (t *Tui) getAddress() {
	t.log("Getting wallet address...")
	res, err := t.walletRpcCall("getAddresses", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Get address failed: %s", err))
		return
	}
	t.log(fmt.Sprintf("Address: %v", res))
}

func (t *Tui) startStake() {
	t.log("Creating stake deposit (80 XFG)...")
	params := map[string]interface{}{
		"amount": int64(80 * 100000000),
		"type":   "elderfier_stake",
	}
	res, err := t.walletRpcCall("create_stake_deposit", params)
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Stake failed: %s", err))
		return
	}
	txHash := fmt.Sprintf("%v", res["tx_hash"])
	t.log(fmt.Sprintf("Stake TX: %s", txHash))
	t.log("Next: Wait 10 blocks, then register_to_enindex with 8-char elder ID")
}

func (t *Tui) checkStake() {
	t.log("Checking stake status...")
	res, err := t.walletRpcCall("get_stake_status", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("No stake: %s", err))
		return
	}
	t.log(fmt.Sprintf("Stake: %v", res))
}

func (t *Tui) viewConsensus() {
	t.log("Querying consensus requests...")
	res, err := t.walletRpcCall("get_consensus_requests", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Consensus unavailable: %s", err))
		return
	}
	t.log(fmt.Sprintf("Requests: %v", res))
}

func (t *Tui) voteOnPending() {
	t.log("Checking pending votes...")
	res, err := t.walletRpcCall("get_pending_votes", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("No pending votes: %s", err))
		return
	}
	t.log(fmt.Sprintf("Pending: %v", res))
	t.log("Use submit_vote RPC to vote (not yet implemented)")
}

func (t *Tui) registerENindex() {
	form := tview.NewForm().
		SetTitle("[cyan]Register to ENindex").
		SetBorder(true)

	var elderID string
	var txHash string

	form.AddInputField("Elderfier ID (8 chars)", "", 8, nil, func(text string) {
		elderID = text
	})
	form.AddInputField("Stake TX Hash", "", 32, nil, func(text string) {
		txHash = text
	})
	form.AddButton("Register", func() {
		if len(elderID) != 8 {
			t.log("ERROR: Elder ID must be 8 characters")
			return
		}

		addrRes, err := t.walletRpcCall("getAddresses", map[string]interface{}{})
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Get address failed: %s", err))
			return
		}
		addr := fmt.Sprintf("%v", addrRes)

		params := map[string]interface{}{
			"elder_id":      elderID,
			"stake_tx_hash": txHash,
			"address":       addr,
		}
		res, err := t.walletRpcCall("register_to_enindex", params)
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Registration failed: %s", err))
			return
		}
		t.log(fmt.Sprintf("ENindex registered: %v", res))
		t.pages.RemovePage("enindex")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("enindex")
	})

	t.pages.AddPage("enindex", form, true, true)
}

func (t *Tui) increaseStake() {
	form := tview.NewForm().
		SetTitle("[cyan]Increase Stake").
		SetBorder(true)

	var amount float64
	form.AddInputField("Additional Amount (XFG)", "10", 10, nil, func(text string) {
		fmt.Sscanf(text, "%f", &amount)
	})
	form.AddButton("Increase", func() {
		params := map[string]interface{}{
			"amount": int64(amount * 100000000),
		}
		res, err := t.walletRpcCall("increase_stake", params)
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Increase failed: %s", err))
			return
		}
		t.log(fmt.Sprintf("Stake increased: %v", res))
		t.pages.RemovePage("increase")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("increase")
	})

	t.pages.AddPage("increase", form, true, true)
}

func (t *Tui) updateENindex() {
	form := tview.NewForm().
		SetTitle("[cyan]Update ENindex").
		SetBorder(true)

	var newID string
	form.AddInputField("New Elderfier ID (8 chars)", "", 8, nil, func(text string) {
		newID = text
	})
	form.AddButton("Update", func() {
		params := map[string]interface{}{}
		if len(newID) == 8 {
			params["elder_id"] = newID
		}
		res, err := t.walletRpcCall("update_enindex", params)
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Update failed: %s", err))
			return
		}
		t.log(fmt.Sprintf("ENindex updated: %v", res))
		t.pages.RemovePage("update")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("update")
	})

	t.pages.AddPage("update", form, true, true)
}

func (t *Tui) burnModal(amount float64) {
	modal := tview.NewModal().
		SetText(fmt.Sprintf("Burn %.2f XFG?\n\nThis will create a burn deposit transaction.", amount)).
		AddButtons([]string{"Burn", "Cancel"}).
		SetDoneFunc(func(buttonIndex int, buttonLabel string) {
			if buttonLabel == "Burn" {
				t.burn(amount)
			}
			t.pages.RemovePage("burnmodal")
		})

	t.pages.AddPage("burnmodal", modal, true, true)
}

func (t *Tui) burnCustom() {
	form := tview.NewForm().
		SetTitle("[orange]Custom Burn Amount").
		SetBorder(true)

	var amount float64
	form.AddInputField("Amount (XFG)", "1.0", 10, nil, func(text string) {
		fmt.Sscanf(text, "%f", &amount)
	})
	form.AddButton("Burn", func() {
		t.burn(amount)
		t.pages.RemovePage("customburn")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("customburn")
	})

	t.pages.AddPage("customburn", form, true, true)
}

func (t *Tui) burn(amount float64) {
	t.log(fmt.Sprintf("Creating burn deposit (%.2f XFG)...", amount))
	params := map[string]interface{}{"amount": int64(amount * 100000000)}
	res, err := t.walletRpcCall("create_burn_deposit", params)
	if err != nil {
		t.log(fmt.Sprintf("ERROR: Burn failed: %s", err))
		return
	}
	txHash := fmt.Sprintf("%v", res["tx_hash"])
	t.log(fmt.Sprintf("Burn TX: %s", txHash))
	t.log("Next: 1) Wait 10+ blocks 2) Request elderfier consensus 3) Generate STARK proof")
}

func (t *Tui) requestConsensus() {
	form := tview.NewForm().
		SetTitle("[orange]Request Elderfier Consensus").
		SetBorder(true)

	var txHash string
	var amount int64
	form.AddInputField("Burn TX Hash", "", 32, nil, func(text string) {
		txHash = text
	})
	form.AddInputField("Amount (atomic units)", "80000000", 15, nil, func(text string) {
		fmt.Sscanf(text, "%d", &amount)
	})
	form.AddButton("Request Proof", func() {
		params := map[string]interface{}{
			"tx_hash": txHash,
			"amount":  amount,
		}
		res, err := t.walletRpcCall("request_elderfier_consensus", params)
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Consensus request failed: %s", err))
			return
		}
		proof := fmt.Sprintf("%v", res["eldernode_proof"])
		t.log(fmt.Sprintf("Eldernode proof received: %s", proof[:32]+"..."))
		t.log("Use this proof with xfg-stark generate-proof")
		t.pages.RemovePage("consensus")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("consensus")
	})

	t.pages.AddPage("consensus", form, true, true)
}

func (t *Tui) generateStark() {
	form := tview.NewForm().
		SetTitle("[orange]Generate STARK Proof").
		SetBorder(true)

	var txHash string
	var amount int64
	var eldernodeProof string

	form.AddInputField("Burn TX Hash", "", 32, nil, func(text string) {
		txHash = text
	})
	form.AddInputField("Amount (atomic)", "80000000", 15, nil, func(text string) {
		fmt.Sscanf(text, "%d", &amount)
	})
	form.AddInputField("Eldernode Proof", "", 64, nil, func(text string) {
		eldernodeProof = text
	})
	form.AddButton("Generate", func() {
		xfgStarkPath := t.binPath("xfg-stark")
		if _, err := exec.LookPath("xfg-stark"); err != nil && xfgStarkPath == "xfg-stark" {
			t.log("ERROR: xfg-stark CLI not found")
			t.log("Manual: xfg-stark generate-proof --tx-hash <hash> --amount <amt> --eldernode-proof <proof>")
			return
		}

		cmd := exec.Command(xfgStarkPath, "generate-proof",
			"--tx-hash", txHash,
			"--amount", fmt.Sprintf("%d", amount),
			"--eldernode-proof", eldernodeProof,
		)

		out, err := cmd.CombinedOutput()
		if err != nil {
			t.log(fmt.Sprintf("ERROR: STARK generation failed: %s", err))
			t.log(string(out))
			return
		}

		t.log("✅ STARK proof generated")
		t.log("Output: " + string(out))
		t.log("Ready for Arbitrum L2 submission!")
		t.pages.RemovePage("stark")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("stark")
	})

	t.pages.AddPage("stark", form, true, true)
}

func (t *Tui) viewAddress() {
	res, err := t.walletRpcCall("getAddresses", map[string]interface{}{})
	if err != nil {
		t.log(fmt.Sprintf("ERROR: No wallet found: %s", err))
		return
	}
	addr := fmt.Sprintf("%v", res)

	form := tview.NewForm().
		SetTitle("[green]Your Wallet Address").
		SetBorder(true)

	form.AddTextView("Address", addr, 64, 1, true)
	form.AddButton("Copy to Clipboard", func() {
		t.log("Address: " + addr)
		t.log("(Clipboard copy simulated - use your OS clipboard)")
	})
	form.AddButton("Close", func() {
		t.pages.RemovePage("viewaddr")
	})

	t.pages.AddPage("viewaddr", form, true, true)
}

func (t *Tui) sendTxForm() {
	form := tview.NewForm().
		SetTitle("[green]Send Transaction").
		SetBorder(true)

	var recipient string
	var amount float64

	form.AddInputField("Recipient Address", "", 64, nil, func(text string) {
		recipient = text
	})
	form.AddInputField("Amount (XFG)", "1.0", 10, nil, func(text string) {
		fmt.Sscanf(text, "%f", &amount)
	})
	form.AddButton("Send", func() {
		if recipient == "" {
			t.log("ERROR: Recipient required")
			return
		}
		params := map[string]interface{}{
			"transfers": []map[string]interface{}{{
				"address": recipient,
				"amount":  int64(amount * 100000000),
			}},
		}
		res, err := t.walletRpcCall("send_transaction", params)
		if err != nil {
			t.log(fmt.Sprintf("ERROR: Send failed: %s", err))
			return
		}
		t.log(fmt.Sprintf("TX sent: %v", res))
		t.pages.RemovePage("sendtx")
	})
	form.AddButton("Cancel", func() {
		t.pages.RemovePage("sendtx")
	})

	t.pages.AddPage("sendtx", form, true, true)
}
