package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gdamore/tcell/v2"
	"github.com/rivo/tview"
)

type AppState struct {
	app           *tview.Application
	pages         *tview.Pages
	network       string
	nodeCmd       *exec.Cmd
	walletCmd     *exec.Cmd
	walletStdin   io.WriteCloser
	walletOutput  []string
	walletMu      sync.Mutex
	walletReady   bool
	logs          []string
	isNodeRunning bool
	isWalletOpen  bool
	statusBar     *tview.TextView
}

var appState AppState

const fuegoLogo = `
    ███████╗██╗   ██╗███████╗ ██████╗  ██████╗
    ██╔════╝██║   ██║██╔════╝██╔════╝ ██╔═══██╗
    █████╗  ██║   ██║█████╗  ██║  ███╗██║   ██║
    ██╔══╝  ██║   ██║██╔══╝  ██║   ██║██║   ██║
    ██║     ╚██████╔╝███████╗╚██████╔╝╚██████╔╝
    ╚═╝      ╚═════╝ ╚══════╝ ╚═════╝  ╚═════╝ `

func main() {
	appState = AppState{
		app:     tview.NewApplication(),
		pages:   tview.NewPages(),
		network: "mainnet",
		logs:    make([]string, 0),
	}
	CurrentConfig = MainnetConfig
	// appState.app.EnableMouse(true) // Disabled to allow native terminal text selection
	appState.app.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Key() == tcell.KeyCtrlC {
			appState.app.Stop()
			return nil
		}
		return event
	})
	tview.Styles.PrimaryTextColor = tcell.ColorOrange
	tview.Styles.SecondaryTextColor = tcell.ColorYellow
	tview.Styles.TertiaryTextColor = tcell.ColorRed
	tview.Styles.BorderColor = tcell.ColorOrange
	tview.Styles.TitleColor = tcell.ColorOrange
	showSplashScreen()
	if err := appState.app.SetRoot(appState.pages, true).SetFocus(appState.pages).Run(); err != nil {
		panic(err)
	}
}

func showSplashScreen() {
	splash := tview.NewTextView().
		SetTextAlign(tview.AlignCenter).
		SetDynamicColors(true)
	splash.SetBackgroundColor(tcell.ColorBlack)
	appState.pages.AddPage("splash", splash, true, true)
	go func() {
		type frame struct {
			color  tcell.Color
			border string
		}
		colors := []frame{
			{tcell.ColorOrange, " *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * "},
			{tcell.ColorYellow, "  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *"},
			{tcell.ColorWhite, " *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * "},
			{tcell.ColorOrange, "*  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  "},
			{tcell.ColorYellow, "  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *"},
			{tcell.ColorWhite, " *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * "},
		}
		for i := 0; i < 6; i++ {
			f := colors[i%len(colors)]
			appState.app.QueueUpdateDraw(func() {
				splash.SetTextColor(f.color)
				splash.SetText(fmt.Sprintf("\n%s\n%s\n%s\n\n         Fuego P2P Blockchain Network(s) TUI\n",
					f.border, fuegoLogo, f.border))
			})
			time.Sleep(300 * time.Millisecond)
		}
		appState.app.QueueUpdateDraw(func() {
			splash.SetTextColor(tcell.ColorOrange)
			splash.SetText(fmt.Sprintf("\n%s\n\n   Money To Burn With CD Returns \n", fuegoLogo))
		})
		time.Sleep(1 * time.Second)
		appState.app.QueueUpdateDraw(func() {
			buildMainMenu()
			appState.pages.SwitchToPage("main")
			appState.pages.RemovePage("splash")
		})
	}()
}

func buildMainMenu() {
	headerText := fmt.Sprintf(" [orange]FUEGO[white] | Network: [yellow]%s[white] | Node: [yellow]%s[white] / Wallet: [yellow]%s[white] | Press [orange]'n'[white] to toggle network",
		CurrentConfig.NetworkName, CurrentConfig.NodeBinary, CurrentConfig.WalletBinary)
	header := tview.NewTextView().SetDynamicColors(true).SetText(headerText).SetBackgroundColor(tcell.ColorBlack)

	list := tview.NewList().
		SetMainTextColor(tcell.ColorOrange).
		SetSecondaryTextColor(tcell.ColorDarkGray).
		SetSelectedTextColor(tcell.ColorBlack).
		SetSelectedBackgroundColor(tcell.ColorOrange)

	list.AddItem("[orange]━━━━━━━━━━━━━━ NODE ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Start Node", fmt.Sprintf("Launch %s daemon", CurrentConfig.NodeBinary), '1', startNode)
	list.AddItem("  Stop Node", "Shut down running daemon", '2', stopNode)
	list.AddItem("  Node Status", "Show height and peer count", '3', showNodeStatus)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ WALLET ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Open Wallet", fmt.Sprintf("Launch %s with existing wallet", CurrentConfig.WalletBinary), '4', openWallet)
	list.AddItem("  Create New Wallet", "Generate a new wallet file", '5', uiCreateWallet)
	list.AddItem("  Close Wallet", "Exit running wallet process", '6', closeWallet)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ INFO ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Balance", "Show wallet balance", 'b', cmdBalance)
	list.AddItem("  Address", "Show wallet address", 'a', cmdAddress)
	list.AddItem("  Blockchain Height", "Show current chain height", 0, cmdBcHeight)
	list.AddItem("  List Transfers", "Show transaction history", 0, cmdListTransfers)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ TRANSFER ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Send Transaction", fmt.Sprintf("Send %s to an address", CurrentConfig.CoinName), 's', uiSendForm)
	list.AddItem("  Atomic Swap", "Swap XFG for other coins", 'w', uiSwapMenu)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ ETHEREAL MINT ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Burn (HEAT)", "Permanently burn coins (0.8/8/80/800)", 'h', uiBurnMenu)
	list.AddItem("  Generate Proof", "Generate STARK proof from burn tx hash", 0, uiGenerateProofForm)
	list.AddItem("  List Burns", "Show all burn transactions", 0, cmdListBurns)
	list.AddItem("  Burn Info", "Detailed info for a burn by ID", 0, uiBurnInfoForm)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ CD BANKING ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  List Deposits", "Show all CD deposits", 0, cmdListDeposits)
	list.AddItem("  Deposit Info", "Detailed info for a deposit by ID", 0, uiDepositInfoForm)
	list.AddItem("  Withdraw Deposit", "Withdraw a matured deposit", 0, uiWithdrawForm)
	list.AddItem("  New CD Deposit", "Lock XFG for interest", 0, uiNewDepositForm)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ ALIASES ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Register Alias", "Register an @ alias", 0, uiRegisterAliasForm)
	list.AddItem("  Lookup Alias", "Look up alias or address", 0, uiLookupAliasForm)
	list.AddItem("  List Aliases", "Show all registered aliases", 0, cmdListAliases)
	list.AddItem("", "", 0, nil)

	list.AddItem("[orange]━━━━━━━━━━━━━━ SYSTEM ━━━━━━━━━━━━━━", "", 0, nil)
	list.AddItem("  Wallet Console", "Send raw commands to wallet", 'c', uiWalletConsole)
	list.AddItem("  Show Logs", "View application log output", 'l', uiShowLogs)
	list.AddItem("  Quit", "Exit the TUI", 'q', func() { appState.app.Stop() })

	statusText := "[green]Ready[white]"
	if appState.isNodeRunning {
		statusText = "[green]Node: Running[white]"
	}
	if appState.isWalletOpen {
		statusText += " | [green]Wallet: Open[white]"
	}
	if appState.statusBar == nil {
		appState.statusBar = tview.NewTextView().SetDynamicColors(true)
		appState.statusBar.SetBackgroundColor(tcell.ColorDarkSlateGray)
	}
	appState.statusBar.SetText(" " + statusText)

	mainLayout := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(header, 1, 0, false).
		AddItem(list, 0, 1, true).
		AddItem(appState.statusBar, 1, 0, false)

	mainLayout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Rune() == 'n' || event.Rune() == 'N' {
			if appState.isNodeRunning || appState.isWalletOpen {
				msgBox("Stop node and close wallet before switching networks")
				return nil
			}
			if appState.network == "mainnet" {
				appState.network = "testnet"
				CurrentConfig = TestnetConfig
			} else {
				appState.network = "mainnet"
				CurrentConfig = MainnetConfig
			}
			appState.pages.RemovePage("main")
			buildMainMenu()
			appState.pages.SwitchToPage("main")
			return nil
		}
		return event
	})

	appState.pages.AddPage("main", mainLayout, true, true)
}

func rebuildMenu() {
	appState.pages.RemovePage("main")
	buildMainMenu()
}

// ============================================================================
// Node
// ============================================================================

func startNode() {
	// Check if a node process is already running on the system
	out, _ := exec.Command("pgrep", "-f", CurrentConfig.NodeBinary).Output()
	if len(out) > 0 {
		msgBox("A " + CurrentConfig.NodeBinary + " process is already running on this system.")
		appState.isNodeRunning = true
		addLog("[INFO] Detected existing " + CurrentConfig.NodeBinary + " process")
		appState.app.QueueUpdateDraw(func() {
			if appState.statusBar != nil {
				appState.statusBar.SetText(" [green]Node: Running (External)[white]")
			}
		})
		return
	}

	if appState.isNodeRunning {
		msgBox("Node is already running")
		return
	}
	bp := findBinary(CurrentConfig.NodeBinary)
	if bp == "" {
		msgBox(CurrentConfig.NodeBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	args := []string{
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
	}
	cmd := exec.Command(bp, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		addLog("[ERROR] Failed to start node: " + err.Error())
		msgBox("Failed to start node: " + err.Error())
		return
	}
	appState.nodeCmd = cmd
	appState.isNodeRunning = true
	addLog("[INFO] Started " + CurrentConfig.NodeBinary)
	
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Node: Starting...[white]")
	}
	msgBox("Node starting...")
	go pipeReader(stdout, "NODE")
	go pipeReader(stderr, "NODE-ERR")
	go func() {
		cmd.Wait()
		appState.isNodeRunning = false
		appState.nodeCmd = nil
		addLog("[INFO] Node process exited")
		appState.app.QueueUpdateDraw(func() { 
			if appState.statusBar != nil {
				appState.statusBar.SetText(" [green]Ready[white]")
			}
		})
	}()
}

func stopNode() {
	if !appState.isNodeRunning {
		msgBox("Node is not running")
		return
	}
	if appState.nodeCmd != nil && appState.nodeCmd.Process != nil {
		appState.nodeCmd.Process.Kill()
	}
	appState.isNodeRunning = false
	appState.nodeCmd = nil
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Ready[white]")
	}
	msgBox("Node stopped")
}

func showNodeStatus() {
	if !appState.isNodeRunning {
		msgBox("Node is not running")
		return
	}
	info, err := fetchNodeInfo()
	if err != nil {
		addLog("[ERROR] Node RPC query failed: " + err.Error())
		msgBox("Node RPC error: " + err.Error() + "\n\nWait for node to fully start or check logs.")
		return
	}
	msgBox(fmt.Sprintf("Node Status\n\nHeight: %d\nPeers: %d", info.Height, info.Peers))
}

		})
		return
	}

	if appState.isNodeRunning {
		msgBox("Node is already running")
		return
	}
	bp := findBinary(CurrentConfig.NodeBinary)
	if bp == "" {
		msgBox(CurrentConfig.NodeBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	args := []string{
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
	}
	cmd := exec.Command(bp, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		addLog("[ERROR] Failed to start node: " + err.Error())
		msgBox("Failed to start node: " + err.Error())
		return
	}
	appState.nodeCmd = cmd
	appState.isNodeRunning = true
	addLog("[INFO] Started " + CurrentConfig.NodeBinary)
	
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Node: Starting...[white]")
	}
	msgBox("Node starting...")
	go pipeReader(stdout, "NODE")
	go pipeReader(stderr, "NODE-ERR")
	go func() {
		cmd.Wait()
		appState.isNodeRunning = false
		appState.nodeCmd = nil
		addLog("[INFO] Node process exited")
		appState.app.QueueUpdateDraw(func() { 
			if appState.statusBar != nil {
				appState.statusBar.SetText(" [green]Ready[white]")
			}
		})
	}()
}

		})
		return
	}

	if appState.isNodeRunning {
		msgBox("Node is already running")
		return
	}
	bp := findBinary(CurrentConfig.NodeBinary)
	if bp == "" {
		msgBox(CurrentConfig.NodeBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	args := []string{
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
	}
	cmd := exec.Command(bp, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		addLog("[ERROR] Failed to start node: " + err.Error())
		msgBox("Failed to start node: " + err.Error())
		return
	}
	appState.nodeCmd = cmd
	appState.isNodeRunning = true
	addLog("[INFO] Started " + CurrentConfig.NodeBinary)
	
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Node: Starting...[white]")
	}
	msgBox("Node starting...")
	go pipeReader(stdout, "NODE")
	go pipeReader(stderr, "NODE-ERR")
	go func() {
		cmd.Wait()
		appState.isNodeRunning = false
		appState.nodeCmd = nil
		addLog("[INFO] Node process exited")
		appState.app.QueueUpdateDraw(func() { 
			if appState.statusBar != nil {
				appState.statusBar.SetText(" [green]Ready[white]")
			}
		})
	}()
}


		})
		return
	}

	if appState.isNodeRunning {
		msgBox("Node is already running")
		return
	}
	bp := findBinary(CurrentConfig.NodeBinary)
	if bp == "" {
		msgBox(CurrentConfig.NodeBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	args := []string{
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
	}
	cmd := exec.Command(bp, args...)
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		addLog("[ERROR] Failed to start node: " + err.Error())
		msgBox("Failed to start node: " + err.Error())
		return
	}
	appState.nodeCmd = cmd
	appState.isNodeRunning = true
	addLog("[INFO] Started " + CurrentConfig.NodeBinary)
	
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Node: Starting...[white]")
	}
	msgBox("Node starting...")
	go pipeReader(stdout, "NODE")
	go pipeReader(stderr, "NODE-ERR")
	go func() {
		cmd.Wait()
		appState.isNodeRunning = false
		appState.nodeCmd = nil
		addLog("[INFO] Node process exited")
		appState.app.QueueUpdateDraw(func() { 
			if appState.statusBar != nil {
				appState.statusBar.SetText(" [green]Ready[white]")
			}
		})
	}()
}

func stopNode() {
	if !appState.isNodeRunning {
		msgBox("Node is not running")
		return
	}
	if appState.nodeCmd != nil && appState.nodeCmd.Process != nil {
		appState.nodeCmd.Process.Kill()
	}
	appState.isNodeRunning = false
	appState.nodeCmd = nil
	if appState.statusBar != nil {
		appState.statusBar.SetText(" [green]Ready[white]")
	}
	msgBox("Node stopped")
}

func showNodeStatus() {
	if !appState.isNodeRunning {
		msgBox("Node is not running")
		return
	}
	info, err := fetchNodeInfo()
	if err != nil {
		addLog("[ERROR] Node RPC query failed: " + err.Error())
		msgBox("Node RPC error: " + err.Error() + "\n\nWait for node to fully start or check logs.")
		return
	}
	msgBox(fmt.Sprintf("Node Status\n\nHeight: %d\nPeers: %d", info.Height, info.Peers))
}

// ============================================================================
// Wallet process (interactive CLI via stdin/stdout)
// ============================================================================

func openWallet() {
	if appState.isWalletOpen {
		msgBox("Wallet is already open")
		return
	}
	bp := findBinary(CurrentConfig.WalletBinary)
	if bp == "" {
		msgBox(CurrentConfig.WalletBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	walletFile := filepath.Join(dataDir, "wallet.wallet")
	if _, err := os.Stat(walletFile); os.IsNotExist(err) {
		msgBox("No wallet file found at:\n" + walletFile + "\n\nCreate a wallet first.")
		return
	}
	form := tview.NewForm()
	pw := tview.NewInputField().SetLabel("Wallet Password").SetFieldWidth(40).SetMaskCharacter('*')
	form.AddFormItem(pw).
		AddButton("Open", func() {
			password := pw.GetText()
			if password == "" {
				msgBox("Password required")
				return
			}
			appState.pages.SwitchToPage("main")
			go spawnWallet(bp, walletFile, password)
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Open Wallet ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("openWallet", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("openWallet")
}

func uiCreateWallet() {
	bp := findBinary(CurrentConfig.WalletBinary)
	if bp == "" {
		msgBox(CurrentConfig.WalletBinary + " not found")
		return
	}
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	walletFile := filepath.Join(dataDir, "wallet.wallet")
	if _, err := os.Stat(walletFile); err == nil {
		msgBox("Wallet already exists at:\n" + walletFile)
		return
	}
	form := tview.NewForm()
	pw := tview.NewInputField().SetLabel("New Password").SetFieldWidth(40).SetMaskCharacter('*')
	confirm := tview.NewInputField().SetLabel("Confirm Password").SetFieldWidth(40).SetMaskCharacter('*')
	form.AddFormItem(pw).AddFormItem(confirm).
		AddButton("Create", func() {
			if pw.GetText() == "" {
				msgBox("Password required")
				return
			}
			if pw.GetText() != confirm.GetText() {
				msgBox("Passwords do not match")
				return
			}
			msgBox("Creating wallet...")
			go func() {
				args := []string{
					fmt.Sprintf("--generate-new-wallet=%s", walletFile),
					fmt.Sprintf("--password=%s", pw.GetText()),
				}
				cmd := exec.Command(bp, args...)
				output, err := cmd.CombinedOutput()
				appState.app.QueueUpdateDraw(func() {
					if err != nil {
						addLog("[WALLET] Error: " + err.Error() + "\n" + string(output))
						msgBox("Error creating wallet. Check logs.")
					} else {
						addLog("[WALLET] Created successfully")
						msgBox("Wallet created!\n" + walletFile + "\n\nUse 'Open Wallet' to connect.")
					}
				})
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Create New Wallet ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("createWallet", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("createWallet")
}

func spawnWallet(binary, walletFile, password string) {
	args := []string{
		fmt.Sprintf("--wallet-file=%s", walletFile),
		fmt.Sprintf("--password=%s", password),
		fmt.Sprintf("--daemon-address=127.0.0.1:%d", CurrentConfig.NodeRPCPort),
	}
	cmd := exec.Command(binary, args...)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		appState.app.QueueUpdateDraw(func() { msgBox("Failed to pipe stdin: " + err.Error()) })
		return
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		appState.app.QueueUpdateDraw(func() { msgBox("Failed to pipe stdout: " + err.Error()) })
		return
	}
	stderr, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		appState.app.QueueUpdateDraw(func() {
			addLog("[WALLET] Failed to start: " + err.Error())
			msgBox("Failed to start wallet: " + err.Error())
		})
		return
	}
	appState.walletCmd = cmd
	appState.walletStdin = stdin
	appState.isWalletOpen = true
	appState.walletReady = false
	appState.walletOutput = nil
	addLog("[INFO] Started " + CurrentConfig.WalletBinary)
	appState.app.QueueUpdateDraw(func() { rebuildMenu() })

	go func() {
		scanner := bufio.NewScanner(stdout)
		for scanner.Scan() {
			line := scanner.Text()
			appState.walletMu.Lock()
			appState.walletOutput = append(appState.walletOutput, line)
			if len(appState.walletOutput) > 500 {
				appState.walletOutput = appState.walletOutput[len(appState.walletOutput)-500:]
			}
			appState.walletMu.Unlock()
			appState.app.QueueUpdateDraw(func() { addLog("[WALLET] " + line) })
			if strings.Contains(line, "[wallet") || strings.Contains(line, "]:") {
				appState.walletReady = true
			}
		}
	}()
	go pipeReader(stderr, "WALLET-ERR")
	go func() {
		cmd.Wait()
		appState.isWalletOpen = false
		appState.walletReady = false
		appState.walletCmd = nil
		appState.walletStdin = nil
		addLog("[INFO] Wallet process exited")
		appState.app.QueueUpdateDraw(func() { rebuildMenu() })
	}()
}

func closeWallet() {
	if !appState.isWalletOpen {
		msgBox("No wallet is open")
		return
	}
	walletSend("exit")
	go func() {
		time.Sleep(2 * time.Second)
		if appState.walletCmd != nil && appState.walletCmd.Process != nil {
			appState.walletCmd.Process.Kill()
		}
	}()
}

func walletSend(cmd string) {
	if !appState.isWalletOpen || appState.walletStdin == nil {
		return
	}
	appState.walletMu.Lock()
	appState.walletOutput = nil
	appState.walletMu.Unlock()
	fmt.Fprintln(appState.walletStdin, cmd)
}

func walletExec(cmd string, waitSec int) []string {
	if !appState.isWalletOpen || appState.walletStdin == nil {
		return []string{"Wallet is not open"}
	}
	appState.walletMu.Lock()
	appState.walletOutput = nil
	appState.walletMu.Unlock()
	fmt.Fprintln(appState.walletStdin, cmd)
	time.Sleep(time.Duration(waitSec) * time.Second)
	appState.walletMu.Lock()
	result := make([]string, len(appState.walletOutput))
	copy(result, appState.walletOutput)
	appState.walletMu.Unlock()
	return result
}

func needWallet() bool {
	if !appState.isWalletOpen {
		msgBox("Wallet is not open.\nOpen a wallet first.")
		return false
	}
	return true
}

// ============================================================================
// Wallet CLI commands
// ============================================================================

func cmdBalance() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("balance", 2)
		appState.app.QueueUpdateDraw(func() { msgBox("Balance\n\n" + strings.Join(lines, "\n")) })
	}()
}

func cmdAddress() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("address", 1)
		appState.app.QueueUpdateDraw(func() { msgBox("Wallet Address\n\n" + strings.Join(lines, "\n")) })
	}()
}

func cmdBcHeight() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("height", 2)
		appState.app.QueueUpdateDraw(func() { msgBox("Blockchain Height\n\n" + strings.Join(lines, "\n")) })
	}()
}

func cmdListTransfers() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("list_transfers", 3)
		appState.app.QueueUpdateDraw(func() { scrollBox("Transfer History", lines) })
	}()
}

func cmdListBurns() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("list_burns", 3)
		appState.app.QueueUpdateDraw(func() { scrollBox("HEAT Burns", lines) })
	}()
}

func cmdListDeposits() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("list_deposits", 3)
		appState.app.QueueUpdateDraw(func() { scrollBox("CD Deposits", lines) })
	}()
}

func cmdListAliases() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("list_aliases", 3)
		appState.app.QueueUpdateDraw(func() { scrollBox("Registered Aliases", lines) })
	}()
}

// Elderking Ceremony is removed.

func uiSwapMenu() {
	if !needWallet() { return }
	list := tview.NewList().
		SetMainTextColor(tcell.ColorOrange).
		SetSelectedTextColor(tcell.ColorBlack).
		SetSelectedBackgroundColor(tcell.ColorOrange)
	list.AddItem("  Create Swap", "Initiate a new atomic swap", 'c', uiCreateSwapForm)
	list.AddItem("  List Active Swaps", "View status of active swaps", 'l', cmdListSwaps)
	list.AddItem("  Swap Info", "Detailed info for swap by ID", 'i', uiSwapInfoForm)
	list.AddItem("  Back", "", 'b', func() { appState.pages.SwitchToPage("main") })
	
	layout := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(tview.NewTextView().SetText(" Atomic Swap Dashboard ").SetTextAlign(tview.AlignCenter).SetTextColor(tcell.ColorOrange), 1, 0, false).
		AddItem(list, 0, 1, true)
	appState.pages.AddPage("swapMenu", layout, true, true)
	appState.pages.SwitchToPage("swapMenu")
}

func uiCreateSwapForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	pair := tview.NewInputField().SetLabel("Pair (e.g. XFG-ETH)").SetFieldWidth(20)
	amt := tview.NewInputField().SetLabel("Amount XFG").SetFieldWidth(20)
	form.AddFormItem(pair).AddFormItem(amt).
		AddButton("Initiate", func() {
			p := pair.GetText()
			a := amt.GetText()
			if p == "" || a == "" { msgBox("Fill fields"); return }
			go func() {
				lines := walletExec("swap_init "+p+" "+a, 5)
				appState.app.QueueUpdateDraw(func() { msgBox("Swap Result\n\n" + strings.Join(lines, "\n")) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("swapMenu") })
	form.SetBorder(true).SetTitle(" Create Atomic Swap ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("createSwap", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("createSwap")
}

func cmdListSwaps() {
	if !needWallet() { return }
	go func() {
		lines := walletExec("list_swaps", 3)
		appState.app.QueueUpdateDraw(func() { scrollBox("Active Swaps", lines) })
	}()
}

func uiSwapInfoForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	sid := tview.NewInputField().SetLabel("Swap ID").SetFieldWidth(30)
	form.AddFormItem(sid).
		AddButton("Get Info", func() {
			id := sid.GetText()
			if id == "" { msgBox("Enter swap ID"); return }
			go func() {
				lines := walletExec("swap_info "+id, 3)
				appState.app.QueueUpdateDraw(func() { scrollBox("Swap Info", lines) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("swapMenu") })
	form.SetBorder(true).SetTitle(" Swap Info ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("swapInfo", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("swapInfo")
}

// ============================================================================
// Transfer UI
// ============================================================================

func uiSendForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	addr := tview.NewInputField().SetLabel("Recipient Address").SetFieldWidth(100)
	amt := tview.NewInputField().SetLabel("Amount (" + CurrentConfig.CoinName + ")").SetFieldWidth(20)
	form.AddFormItem(addr).AddFormItem(amt).
		AddButton("Send", func() {
			if addr.GetText() == "" || amt.GetText() == "" {
				msgBox("Fill all fields")
				return
			}
			walletCmd := fmt.Sprintf("transfer %s %s", addr.GetText(), amt.GetText())
			modal := tview.NewModal().
				SetText(fmt.Sprintf("Send %s %s to\n%s?", amt.GetText(), CurrentConfig.CoinName, addr.GetText())).
				AddButtons([]string{"Confirm", "Cancel"}).
				SetDoneFunc(func(_ int, label string) {
					if label == "Confirm" {
						go func() {
							lines := walletExec(walletCmd, 5)
							appState.app.QueueUpdateDraw(func() { msgBox("Transfer Result\n\n" + strings.Join(lines, "\n")) })
						}()
					} else {
						appState.pages.SwitchToPage("main")
					}
				})
			appState.pages.AddPage("confirmSend", modal, true, true)
			appState.pages.SwitchToPage("confirmSend")
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Send " + CurrentConfig.CoinName + " ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("sendTx", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("sendTx")
}

// ============================================================================
// Burns (HEAT) / Ethereal Mint
// ============================================================================

func uiBurnMenu() {
	if !needWallet() { return }
	list := tview.NewList().
		SetMainTextColor(tcell.ColorOrange).
		SetSelectedTextColor(tcell.ColorBlack).
		SetSelectedBackgroundColor(tcell.ColorOrange)
	labels := []string{"0.8", "8", "80", "800"}
	for i := range CurrentConfig.BurnTiers {
		label := labels[i]
		list.AddItem(fmt.Sprintf("  Burn %s %s", label, CurrentConfig.CoinName), "HEAT deposit (permanent burn)", 0,
			func() { uiConfirmBurn(label) })
	}
	list.AddItem("  Back", "", 0, func() { appState.pages.SwitchToPage("main") })
	title := tview.NewTextView().SetText("The Ethereal Mint - HEAT Burns").SetTextColor(tcell.ColorOrange).SetTextAlign(tview.AlignCenter)
	layout := tview.NewFlex().SetDirection(tview.FlexRow).AddItem(title, 1, 0, false).AddItem(list, 0, 1, true)
	appState.pages.AddPage("burnMenu", layout, true, true)
	appState.pages.SwitchToPage("burnMenu")
}

func uiConfirmBurn(amountStr string) {
	modal := tview.NewModal().
		SetText(fmt.Sprintf("Burn %s %s permanently?\n\nThis CANNOT be undone.\nCoins are destroyed forever.", amountStr, CurrentConfig.CoinName)).
		AddButtons([]string{"BURN IT", "Cancel"}).
		SetDoneFunc(func(_ int, label string) {
			if label == "BURN IT" {
				go func() {
					lines := walletExec("burn "+amountStr, 5)
					appState.app.QueueUpdateDraw(func() { msgBox("Burn Result\n\n" + strings.Join(lines, "\n")) })
				}()
			} else {
				appState.pages.SwitchToPage("burnMenu")
			}
		})
	appState.pages.AddPage("confirmBurn", modal, true, true)
	appState.pages.SwitchToPage("confirmBurn")
}

func uiGenerateProofForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	txHash := tview.NewInputField().SetLabel("Transaction Hash").SetFieldWidth(70)
	form.AddFormItem(txHash).
		AddButton("Generate Proof", func() {
			hash := txHash.GetText()
			if hash == "" { msgBox("Enter a transaction hash"); return }
			go func() {
				lines := walletExec("gen_proof "+hash, 5)
				appState.app.QueueUpdateDraw(func() { scrollBox("use xfg-stark-cli for actual STARK Proof", lines) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Generate STARK Proof ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("genProof", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("genProof")
}

func uiBurnInfoForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	burnID := tview.NewInputField().SetLabel("Burn ID").SetFieldWidth(20)
	form.AddFormItem(burnID).
		AddButton("Get Info", func() {
			id := burnID.GetText()
			if id == "" { msgBox("Enter a burn ID"); return }
			go func() {
				lines := walletExec("burn_info "+id, 3)
				appState.app.QueueUpdateDraw(func() { scrollBox("Burn Info", lines) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Burn Info ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("burnInfo", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("burnInfo")
}

func uiNewDepositForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	amt := tview.NewInputField().SetLabel("Amount (" + CurrentConfig.CoinName + ")").SetFieldWidth(20)
	term := tview.NewInputField().SetLabel("Term (blocks)").SetFieldWidth(20)
	form.AddFormItem(amt).AddFormItem(term).
		AddButton("Deposit", func() {
			a := amt.GetText()
			t := term.GetText()
			if a == "" || t == "" { msgBox("Fill fields"); return }
			go func() {
				lines := walletExec("deposit "+a+" "+t, 5)
				appState.app.QueueUpdateDraw(func() { msgBox("Deposit Result\n\n" + strings.Join(lines, "\n")) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" New CD Deposit ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("newDep", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("newDep")
}

// ============================================================================
// Withdraw Deposit (Restoration)
// ============================================================================


func uiDepositInfoForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	depID := tview.NewInputField().SetLabel("Deposit ID").SetFieldWidth(20)
	form.AddFormItem(depID).
		AddButton("Get Info", func() {
			id := depID.GetText()
			if id == "" { msgBox("Enter a deposit ID"); return }
			go func() {
				lines := walletExec("deposit_info "+id, 3)
				appState.app.QueueUpdateDraw(func() { scrollBox("Deposit Info", lines) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Deposit Info ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("depInfo", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("depInfo")
}

func uiWithdrawForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	depID := tview.NewInputField().SetLabel("Deposit ID").SetFieldWidth(20)
	form.AddFormItem(depID).
		AddButton("Withdraw", func() {
			id := depID.GetText()
			if id == "" { msgBox("Enter a deposit ID"); return }
			go func() {
				lines := walletExec("withdraw "+id, 5)
				appState.app.QueueUpdateDraw(func() { msgBox("Withdrawal Result\n\n" + strings.Join(lines, "\n")) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Withdraw Deposit ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("withdraw", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("withdraw")
}

// ============================================================================
// Aliases
// ============================================================================

func uiRegisterAliasForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	alias := tview.NewInputField().SetLabel("Alias (8 chars)").SetFieldWidth(20)
	form.AddFormItem(alias).
		AddButton("Register", func() {
			a := alias.GetText()
			if a == "" { msgBox("Enter an alias"); return }
			go func() {
				lines := walletExec("register_alias "+a, 5)
				appState.app.QueueUpdateDraw(func() { msgBox("Register Alias\n\n" + strings.Join(lines, "\n")) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Register @ Alias ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("regAlias", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("regAlias")
}

func uiLookupAliasForm() {
	if !needWallet() { return }
	form := tview.NewForm()
	query := tview.NewInputField().SetLabel("Alias or Address").SetFieldWidth(100)
	form.AddFormItem(query).
		AddButton("Lookup", func() {
			q := query.GetText()
			if q == "" { msgBox("Enter an alias or address"); return }
			go func() {
				lines := walletExec("lookup_alias "+q, 3)
				appState.app.QueueUpdateDraw(func() { msgBox("Alias Lookup\n\n" + strings.Join(lines, "\n")) })
			}()
		}).
		AddButton("Cancel", func() { appState.pages.SwitchToPage("main") })
	form.SetBorder(true).SetTitle(" Lookup Alias ").SetTitleAlign(tview.AlignLeft)
	appState.pages.AddPage("lookupAlias", tview.NewFlex().SetDirection(tview.FlexRow).AddItem(form, 0, 1, true), true, true)
	appState.pages.SwitchToPage("lookupAlias")
}

// ============================================================================
// Wallet Console
// ============================================================================

func uiWalletConsole() {
	if !needWallet() { return }
	outputView := tview.NewTextView().SetDynamicColors(true).SetScrollable(true).SetWrap(true)
	outputView.SetBorder(true).SetTitle(" Wallet Output ").SetTitleAlign(tview.AlignLeft)
	inputField := tview.NewInputField().SetLabel(CurrentConfig.WalletBinary + "> ").SetFieldWidth(0).SetFieldBackgroundColor(tcell.ColorBlack)
	inputField.SetDoneFunc(func(key tcell.Key) {
		if key == tcell.KeyEnter {
			cmd := inputField.GetText()
			if cmd == "" { return }
			inputField.SetText("")
			go func() {
				lines := walletExec(cmd, 3)
				appState.app.QueueUpdateDraw(func() {
					outputView.SetText(strings.Join(lines, "\n"))
					outputView.ScrollToEnd()
				})
			}()
		}
	})
	layout := tview.NewFlex().SetDirection(tview.FlexRow).AddItem(outputView, 0, 1, false).AddItem(inputField, 1, 0, true)
	layout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Key() == tcell.KeyEsc { appState.pages.SwitchToPage("main"); return nil }
		return event
	})
	appState.pages.AddPage("console", layout, true, true)
	appState.pages.SwitchToPage("console")
}

// ============================================================================
// Logs
// ============================================================================

func uiShowLogs() {
	logText := strings.Join(appState.logs, "\n")
	if logText == "" { logText = "No logs yet." }
	tv := tview.NewTextView().SetText(logText).SetScrollable(true).SetWrap(true)
	tv.SetBorder(true).SetTitle(" Logs ").SetTitleAlign(tview.AlignLeft)
	layout := tview.NewFlex().SetDirection(tview.FlexRow).AddItem(tv, 0, 1, true)
	layout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Key() == tcell.KeyEsc || event.Rune() == 'q' { appState.pages.SwitchToPage("main"); return nil }
		return event
	})
	appState.pages.AddPage("logs", layout, true, true)
	appState.pages.SwitchToPage("logs")
}

// ============================================================================
// UI helpers
// ============================================================================

func msgBox(message string) {
	modal := tview.NewModal().SetText(message).
		AddButtons([]string{"OK"}).
		SetDoneFunc(func(_ int, _ string) { appState.pages.SwitchToPage("main") })
	appState.pages.AddPage("message", modal, true, true)
	appState.pages.SwitchToPage("message")
}

func scrollBox(title string, lines []string) {
	text := strings.Join(lines, "\n")
	if text == "" { text = "(no output)" }
	tv := tview.NewTextView().SetText(text).SetScrollable(true).SetWrap(true)
	tv.SetBorder(true).SetTitle(" " + title + " ").SetTitleAlign(tview.AlignLeft)
	layout := tview.NewFlex().SetDirection(tview.FlexRow).AddItem(tv, 0, 1, true)
	layout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Key() == tcell.KeyEsc || event.Rune() == 'q' { appState.pages.SwitchToPage("main"); return nil }
		return event
	})
	appState.pages.AddPage("scrollOutput", layout, true, true)
	appState.pages.SwitchToPage("scrollOutput")
}

func addLog(msg string) {
	appState.logs = append(appState.logs, msg)
	if len(appState.logs) > 1000 { appState.logs = appState.logs[len(appState.logs)-1000:] }
}

// ============================================================================
// I/O
// ============================================================================

func pipeReader(r io.Reader, prefix string) {
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := scanner.Text()
		if line != "" {
			appState.app.QueueUpdateDraw(func() { addLog(fmt.Sprintf("[%s] %s", prefix, line)) })
		}
	}
}

func findBinary(name string) string {
	cwd, _ := os.Getwd()
	
	// If we are running from the tui directory, we need to adjust paths
	baseDir := cwd
	if filepath.Base(cwd) == "tui" {
		baseDir = filepath.Dir(cwd)
	}

	paths := []string{
		filepath.Join(baseDir, "build", "src", name),
		filepath.Join(baseDir, "build", "release", "src", name),
		filepath.Join(baseDir, "build-test", "src", name),
		filepath.Join(baseDir, "bulid3", "release", "src", name),
		filepath.Join("/home/ar/fuego", "build", "release", "src", name),
		filepath.Join("/home/ar/fuego", "build", "src", name),
		filepath.Join("/home/ar/fuego", "build-test", "src", name),
		filepath.Join("/home/ar/fuego", "bulid3", "release", "src", name),
	}
	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			addLog(fmt.Sprintf("[DEBUG] Found %s at %s", name, p))
			return p
		}
	}
	if p, err := exec.LookPath(name); err == nil {
		addLog(fmt.Sprintf("[DEBUG] Found %s in PATH: %s", name, p))
		return p
	}
	addLog(fmt.Sprintf("[ERROR] Binary not found: %s", name))
	return ""
}

// ============================================================================
// Node RPC (daemon only - plain HTTP)
// ============================================================================

type NodeInfo struct {
	Height int
	Peers  int
}

func fetchNodeInfo() (*NodeInfo, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/getinfo", CurrentConfig.NodeRPCPort)
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(url)
	if err != nil { return nil, err }
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK { return nil, fmt.Errorf("HTTP %d", resp.StatusCode) }
	var data map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&data); err != nil { return nil, fmt.Errorf("invalid JSON: %w", err) }
	info := &NodeInfo{}
	if h, ok := data["height"]; ok {
		if v, ok := h.(float64); ok { info.Height = int(v) }
	}
	peers := 0
	if ic, ok := data["incoming_connections_count"]; ok {
		if v, ok := ic.(float64); ok { peers += int(v) }
	}
	if oc, ok := data["outgoing_connections_count"]; ok {
		if v, ok := oc.(float64); ok { peers += int(v) }
	}
	info.Peers = peers
	return info, nil
}
