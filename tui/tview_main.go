package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gdamore/tcell/v2"
	"github.com/rivo/tview"
)

// RPCResponse represents a JSON-RPC response
type RPCResponse struct {
	ID      interface{} `json:"id"`
	JSONRPC string      `json:"jsonrpc"`
	Result  interface{} `json:"result,omitempty"`
	Error   *RPCError   `json:"error,omitempty"`
}

// RPCError represents a JSON-RPC error
type RPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// NodeInfo represents node information
type NodeInfo struct {
	Height int `json:"height"`
	Peers  int `json:"peers"`
}

// AppState represents the application state
type AppState struct {
	app             *tview.Application
	pages           *tview.Pages
	network         string
	nodeCmd         *exec.Cmd
	walletCmd       *exec.Cmd
	logs            []string
	isNodeRunning   bool
	isWalletRunning bool
}

var appState AppState

func main() {
	// Initialize application state
	appState = AppState{
		app:             tview.NewApplication(),
		pages:           tview.NewPages(),
		network:         "mainnet",
		logs:            make([]string, 0),
		isNodeRunning:   false,
		isWalletRunning: false,
	}

	// Set initial config
	CurrentConfig = MainnetConfig

	// Create the main menu
	createMainMenu()

	// Enable mouse support
	appState.app.EnableMouse(true)

	// Set fire-themed colors
	tview.Styles.PrimaryTextColor = tcell.ColorOrange
	tview.Styles.SecondaryTextColor = tcell.ColorYellow
	tview.Styles.TertiaryTextColor = tcell.ColorRed
	tview.Styles.BorderColor = tcell.ColorOrange
	tview.Styles.TitleColor = tcell.ColorOrange

	// Run the application
	if err := appState.app.SetRoot(appState.pages, true).SetFocus(appState.pages).Run(); err != nil {
		panic(err)
	}
}

// createMainMenu creates the main menu screen
func createMainMenu() {
	createMainMenuWithNetwork(appState.network)
}

// createMainMenuWithNetwork creates the main menu screen with specified network
func createMainMenuWithNetwork(network string) {
	// Define network-specific ASCII art titles
	var selectedTitle string
	if network == "testnet" {
		// Testnet specific ASCII art
		selectedTitle = "░▀█▀░█▀▀░█▀▀░▀█▀░█▀█░█▀▀░▀█▀░░░█▀▀░█░█░▀█▀░▀█▀░█▀▀\n░░█░░█▀▀░▀▀█░░█░░█░█░█▀▀░░█░░░░▀▀█░█░█░░█░░░█░░█▀▀\n░░▀░░▀▀▀░▀▀▀░░▀░░▀░▀░▀▀▀░░▀░░░░▀▀▀░▀▀▀░▀▀▀░░▀░░▀▀▀"
	} else {
		// Mainnet specific ASCII art
		asciiTitles := []string{
			"░█▀▀░█░█░█▀▀░█▀▀░█▀█░░░█▀▀░█░█░▀█▀░▀█▀░█▀▀\n░█▀▀░█░█░█▀▀░█░█░█░█░░░▀▀█░█░█░░█░░░█░░█▀▀\n░▀░░░▀▀▀░▀▀▀░▀▀▀░▀▀▀░░░▀▀▀░▀▀▀░▀▀▀░░▀░░▀▀▀",
		}
		// Select a random ASCII art title for mainnet
		rand.Seed(time.Now().UnixNano())
		selectedTitle = asciiTitles[rand.Intn(len(asciiTitles))]
	}

	// Create network selection as a simple text display with toggle instruction
	networkText := tview.NewTextView().
			SetText(fmt.Sprintf("Network: %s (Press 'n' to toggle)", strings.Title(network))).
			SetTextColor(tview.Styles.PrimaryTextColor).
			SetBackgroundColor(tcell.ColorBlack)

	// Create title with ASCII art and text below
	title := tview.NewTextView()
	title.SetText(fmt.Sprintf("%s\n\nFuego %s TUI", selectedTitle, strings.Title(network)))
	title.SetTextColor(tview.Styles.TitleColor)
	title.SetTextAlign(tview.AlignCenter)
	title.SetBackgroundColor(tcell.ColorBlack)
	title.SetDynamicColors(true)

	// Add simple color animation to title
	go func() {
		colors := []tcell.Color{tcell.ColorRed, tcell.ColorOrange, tcell.ColorYellow, tcell.ColorGreen, tcell.ColorBlue, tcell.ColorPurple}
		for i := 0; i < 20 && appState.app != nil; i++ {
			colorIndex := i % len(colors)
			// Update title color
			appState.app.QueueUpdateDraw(func() {
				title.SetTextColor(colors[colorIndex])
			})
			time.Sleep(150 * time.Millisecond)
		}
		// Reset to default color
		appState.app.QueueUpdateDraw(func() {
			title.SetTextColor(tview.Styles.TitleColor)
		})
	}()



	// Create main menu items

	// Create main menu items
	menuItems := []struct {
		text   string
		action func()
	}{
		{"Start Node", startNode},
		{"Stop Node", stopNode},
		{"Node Status", showNodeStatus},
		{"Start walletd", startWalletRPC},
		{"Create Wallet", createWallet},
		{"Get Balance", getBalance},
		{"Send Transaction", showSendTransactionForm},
		{"Ælder Kings Council", showElderfierMenu},
		{"Ξternal Flame Menu", showBurn2MintMenu},
		{"Show Logs", showLogs},
		{"Quit", func() { appState.app.Stop() }},
	}

	// Create list for menu items
	list := tview.NewList().
		SetMainTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedBackgroundColor(tview.Styles.MoreContrastBackgroundColor)

	for _, item := range menuItems {
		list.AddItem(item.text, "", 0, item.action)
	}



	// Create layout
	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(tview.NewFlex().
			AddItem(tview.NewBox().SetBorder(false), 1, 0, false).
			AddItem(networkText, 20, 0, false).
			AddItem(tview.NewBox().SetBorder(false), 0, 1, false), 3, 0, false).
		AddItem(list, 0, 1, true)

	mainLayout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(title, 3, 0, false).
		AddItem(layout, 0, 1, true)

	// Handle key events for network toggle
	mainLayout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Rune() == 'n' || event.Rune() == 'N' {
			// Toggle network
			if appState.network == "mainnet" {
				appState.network = "testnet"
				CurrentConfig = TestnetConfig
			} else {
				appState.network = "mainnet"
				CurrentConfig = MainnetConfig
			}
			// Recreate the main menu with the new network
			appState.pages.RemovePage("main")
			createMainMenu()
			appState.pages.SwitchToPage("main")
			return nil
		}
		return event
	})

	appState.pages.AddPage("main", mainLayout, true, true)
}

// startNode starts the Fuego node
func startNode() {
	if appState.isNodeRunning {
		showMessage("Node is already running")
		return
	}

	binPath := binPath(CurrentConfig.NodeBinary)
	if binPath == "" {
		showMessage("Node binary not found")
		appState.logs = append(appState.logs, "[ERROR] Node binary not found")
		return
	}

	// Use network-specific data directory
	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	appState.logs = append(appState.logs, fmt.Sprintf("[INFO] Using data directory: %s", dataDir))

	// Start with network-specific configuration
	cmd := exec.Command(binPath,
		fmt.Sprintf("--p2p-bind-port=%d", CurrentConfig.NodeP2PPort),
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.NodeRPCPort),
		fmt.Sprintf("--data-dir=%s", dataDir),
	)

	// Set up pipes for stdout and stderr like original
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()

	// Start the command asynchronously like original
	if err := cmd.Start(); err != nil {
		appState.logs = append(appState.logs, "[ERROR] Failed to start node: " + err.Error())
		showMessage("Failed to start node: " + err.Error())
		return
	}

	appState.nodeCmd = cmd
	appState.isNodeRunning = true
	appState.logs = append(appState.logs, "[INFO] Started fuegod")
	showMessage("Node starting")

	// Handle output streams asynchronously like the original
	go streamPipe(stdout, "NODE", &appState)
	go streamPipe(stderr, "NODE-ERR", &appState)

	// Wait for RPC to initialize before querying like original
	go func() {
		time.Sleep(3 * time.Second)
		for appState.isNodeRunning && appState.nodeCmd != nil {
			info, err := getNodeInfo()
			if err == nil {
				appState.app.QueueUpdateDraw(func() {
					appState.logs = append(appState.logs, fmt.Sprintf("[NODE] Height: %d, Peers: %d", info.Height, info.Peers))
				})
			} else {
				// Log errors less frequently to avoid spam like original
				appState.app.QueueUpdateDraw(func() {
					shouldLog := len(appState.logs) == 0 || !strings.Contains(appState.logs[len(appState.logs)-1], "Failed to query node")
					if shouldLog {
						appState.logs = append(appState.logs, "[NODE] Failed to query node: " + err.Error())
					}
				})
			}
			time.Sleep(5 * time.Second)
		}
	}()
}

// stopNode stops the Fuego node
func stopNode() {
	if !appState.isNodeRunning {
		showMessage("Node is not running")
		return
	}

	if appState.nodeCmd != nil && appState.nodeCmd.Process != nil {
		err := appState.nodeCmd.Process.Kill()
		if err != nil {
			showMessage("Error stopping node: " + err.Error())
			return
		}
	}

	appState.isNodeRunning = false
	showMessage("Node stopped successfully")
}

// showNodeStatus displays node status information
func showNodeStatus() {
	if !appState.isNodeRunning {
		showMessage("Node is not running")
		return
	}

	info, err := getNodeInfo()
	if err != nil {
		showMessage("Error getting node info: " + err.Error())
		return
	}

	statusText := fmt.Sprintf("Node Status\n\nHeight: %d\nPeers: %d", info.Height, info.Peers)
	showMessage(statusText)
}

// startWalletRPC starts the wallet RPC server
func startWalletRPC() {
	if appState.isWalletRunning {
		showMessage("Wallet RPC is already running")
		return
	}

	binPath := binPath(CurrentConfig.WalletBinary)
	if binPath == "" {
		showMessage("Wallet binary not found")
		appState.logs = append(appState.logs, "[ERROR] Wallet binary not found")
		return
	}

	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)
	appState.logs = append(appState.logs, fmt.Sprintf("[INFO] Using data directory: %s", dataDir))

	walletFile := filepath.Join(dataDir, "wallet.wallet")

	// Check if wallet exists
	if _, err := os.Stat(walletFile); os.IsNotExist(err) {
		showMessage("Wallet file not found. Please create a wallet first.")
		appState.logs = append(appState.logs, fmt.Sprintf("[WARN] Wallet file not found: %s", walletFile))
		return
	}

	// Start with network-specific configuration like original
	cmd := exec.Command(binPath,
		fmt.Sprintf("--rpc-bind-port=%d", CurrentConfig.WalletRPCPort),
		fmt.Sprintf("--wallet-file=%s", walletFile),
		fmt.Sprintf("--daemon-address=127.0.0.1:%d", CurrentConfig.NodeRPCPort),
	)

	// Set up pipes for stdout and stderr like original
	stdout, _ := cmd.StdoutPipe()
	stderr, _ := cmd.StderrPipe()

	// Start the command asynchronously like original
	if err := cmd.Start(); err != nil {
		appState.logs = append(appState.logs, "[ERROR] Failed to start wallet RPC: " + err.Error())
		showMessage("Failed to start wallet RPC: " + err.Error())
		return
	}

	appState.walletCmd = cmd
	appState.isWalletRunning = true
	appState.logs = append(appState.logs, "[INFO] Started walletd")
	showMessage("Wallet RPC starting")

	// Handle output streams asynchronously like original
	go streamPipe(stdout, "WALLET", &appState)
	go streamPipe(stderr, "WALLET-ERR", &appState)
}

// createWallet creates a new wallet
func createWallet() {
	binPath := binPath(CurrentConfig.WalletBinary)
	if binPath == "" {
		showMessage("Wallet binary not found")
		return
	}

	dataDir := filepath.Join(os.Getenv("HOME"), CurrentConfig.DataDir)
	os.MkdirAll(dataDir, 0755)

	walletFile := filepath.Join(dataDir, "wallet.wallet")

	// Check if wallet already exists
	if _, err := os.Stat(walletFile); err == nil {
		showMessage("Wallet already exists. Please delete it first if you want to create a new one.")
		return
	}

	showMessage("Creating wallet... This may take a moment.")

	// Run wallet creation in a separate goroutine to prevent blocking
	go func() {
		cmd := exec.Command(binPath,
			"--wallet-file", walletFile,
			"--daemon-address", "127.0.0.1:"+strconv.Itoa(CurrentConfig.NodeRPCPort),
			"--generate-new-wallet")

		// Set up logging
		stdout, err := cmd.StdoutPipe()
		if err != nil {
			appState.app.QueueUpdateDraw(func() {})
		stderr, _ := cmd.StderrPipe()

	err := cmd.Start()
	if err != nil {
		showMessage("Error starting wallet creation: " + err.Error())
		return
	}

	// Log output in background
	go logStream(stdout, "WALLET-CREATE")
	go logStream(stderr, "WALLET-CREATE-ERR")

	// Wait for completion
	go func() {
		err := cmd.Wait()
		appState.app.QueueUpdateDraw(func() {
			if err != nil {
				appState.logs = append(appState.logs, "[WALLET-CREATE] Error creating wallet: " + err.Error())
				showMessage("Error creating wallet. Check logs for details.")
			} else {
				appState.logs = append(appState.logs, "[WALLET-CREATE] Wallet created successfully")
				showMessage("Wallet created successfully!")
			}
		})
	}()
}

// getBalance gets the wallet balance

func (app *AppState) getBalance() {
	if !appState.isWalletRunning {
		showMessage("Wallet RPC is not running")
		return
	}

	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.GetBalanceRPC, nil)
	if err != nil {
		showMessage("Error getting balance: " + err.Error())
		return
	}

	balance, ok := result["balance"]
	if !ok {
		showMessage("Unexpected response format")
		return
	}

	balanceStr := fmt.Sprintf("%v", balance)
	balanceInt, err := strconv.ParseInt(balanceStr, 10, 64)
	if err != nil {
		showMessage("Error parsing balance: " + err.Error())
		return
	}

	// Convert from atomic units
	balanceXFG := float64(balanceInt) / float64(CurrentConfig.CoinUnits)
	showMessage(fmt.Sprintf("Balance: %.8f %s", balanceXFG, CurrentConfig.CoinName))
}


// showSendTransactionForm displays the send transaction form
func (app *AppState) showSendTransactionForm() {
	if !appState.isWalletRunning {
		showMessage("Wallet RPC is not running")
		return
	}

	// Create form
	form := tview.NewForm()
	addressField := tview.NewInputField().SetLabel("Recipient Address").SetFieldWidth(60)
	amountField := tview.NewInputField().SetLabel("Amount (" + CurrentConfig.CoinName + ")").SetFieldWidth(20)

	form.AddFormItem(addressField).
		AddFormItem(amountField).
		AddButton("Send", func() {
			address := addressField.GetText()
			amountStr := amountField.GetText()

			if address == "" || amountStr == "" {
				showMessage("Please fill all fields")
				return
			}

			amount, err := strconv.ParseFloat(amountStr, 64)
			if err != nil {
				showMessage("Invalid amount")
				return
			}

			// Convert to atomic units
			amountAtomic := int64(amount * float64(CurrentConfig.CoinUnits))

			params := map[string]interface{}{
				"transfers": []map[string]interface{}{
					{"address": address, "amount": amountAtomic},
				},
			}

			result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.SendTransactionRPC, params)
			if err != nil {
				showMessage("Error sending transaction: " + err.Error())
				return
			}

			txHash, ok := result["tx_hash"]
			if !ok {
				showMessage("Unexpected response format")
				return
			}

			showMessage(fmt.Sprintf("Transaction sent successfully!\nTX Hash: %v", txHash))
		}).
		AddButton("Cancel", func() {
			appState.pages.SwitchToPage("main")
		})

	form.SetBorder(true).SetTitle("Send Transaction").SetTitleAlign(tview.AlignLeft)

	// Create layout
	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(form, 0, 1, true)

	appState.pages.AddPage("sendTx", layout, true, true)
	appState.pages.SwitchToPage("sendTx")
}

// showElderfierMenu displays the elderfier menu
func (app *AppState) showElderfierMenu() {
	if !appState.isWalletRunning {
		showMessage("Wallet RPC is not running")
		return
	}

	// Check stake status
	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.GetStakeStatusRPC, nil)
	hasStake := err == nil && result != nil

	menuItems := []struct {
		text   string
		action func()
	}{
		{"View Consensus Requests", viewConsensusRequests},
		{"Vote on Pending Items", voteOnPendingItems},
		{"Review Burn2Mint Requests", reviewBurn2MintRequests},
		{"Manage Stake", manageStake},
		{"Update ENindex Keys", updateENindexKeys},
		{"Back to Main Menu", func() { appState.pages.SwitchToPage("main") }},
	}

	if !hasStake {
		menuItems = []struct {
			text   string
			action func()
		}{
			{"Start Elderfyre Stayking Process", startElderfyreStayking},
			{"Check Stake Status", checkStakeStatus},
			{"Back to Main Menu", func() { appState.pages.SwitchToPage("main") }},
		}
	}

	// Create list for menu items
	list := tview.NewList().
		SetMainTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedBackgroundColor(tview.Styles.MoreContrastBackgroundColor)

	for _, item := range menuItems {
		list.AddItem(item.text, "", 0, item.action)
	}

	// Create layout
	title := tview.NewTextView().
		SetText("👑 Elderfier Dashboard").
		SetTextColor(tview.Styles.PrimaryTextColor).
		SetTextAlign(tview.AlignCenter)

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(title, 3, 0, false).
		AddItem(list, 0, 1, true)

	appState.pages.AddPage("elderfier", layout, true, true)
	appState.pages.SwitchToPage("elderfier")
}

// startElderfyreStayking starts the elderfyre stayking process
func startElderfyreStayking() {
	form := tview.NewForm()
	stakeAmountField := tview.NewInputField().SetLabel("Stake Amount (" + CurrentConfig.CoinName + ")").SetFieldWidth(20).SetText("800")
	elderfierIDField := tview.NewInputField().SetLabel("Elderfier ID (8 chars)").SetFieldWidth(20)

	form.AddFormItem(stakeAmountField).
		AddFormItem(elderfierIDField).
		AddButton("Create Stake", func() {
			amountStr := stakeAmountField.GetText()
			elderfierID := elderfierIDField.GetText()

			amount, err := strconv.ParseFloat(amountStr, 64)
			if err != nil {
				showMessage("Invalid amount")
				return
			}

			if len(elderfierID) != 8 {
				showMessage("Elderfier ID must be exactly 8 characters")
				return
			}

			// Create stake deposit
			amountAtomic := int64(amount * float64(CurrentConfig.CoinUnits))
			params := map[string]interface{}{"amount": amountAtomic}

			result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.CreateStakeRPC, params)
			if err != nil {
				showMessage("Error creating stake: " + err.Error())
				return
			}

			txHash, ok := result["tx_hash"]
			if !ok {
				showMessage("Unexpected response format")
				return
			}

			// Show confirmation and next steps
			message := fmt.Sprintf("🎉 Stake Deposit Created!\n\nTX Hash: %v\n\nNext steps:\n1. Wait for 10 confirmations\n2. Register your Elderfier ID", txHash)
			showMessage(message)
		}).
		AddButton("Cancel", func() {
			appState.pages.SwitchToPage("elderfier")
		})

	form.SetBorder(true).SetTitle("Elderfyre Stayking Process").SetTitleAlign(tview.AlignLeft)

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(form, 0, 1, true)

	appState.pages.AddPage("stayking", layout, true, true)
	appState.pages.SwitchToPage("stayking")
}

// checkStakeStatus checks the stake status
func checkStakeStatus() {
	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.GetStakeStatusRPC, nil)
	if err != nil {
		showMessage("Error checking stake status: " + err.Error())
		return
	}

	statusStr := fmt.Sprintf("Stake Status: %v", result)
	showMessage(statusStr)
}

// viewConsensusRequests views consensus requests
func viewConsensusRequests() {
	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.GetConsensusRPC, nil)
	if err != nil {
		showMessage("Error getting consensus requests: " + err.Error())
		return
	}

	message := fmt.Sprintf("Consensus Requests:\n%v", result)
	showMessage(message)
}

// voteOnPendingItems votes on pending items
func voteOnPendingItems() {
	// Get pending votes
	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.GetPendingVotesRPC, nil)
	if err != nil {
		showMessage("Error getting pending votes: " + err.Error())
		return
	}

	// For now, just show the votes
	message := fmt.Sprintf("Pending Votes:\n%v", result)
	showMessage(message)
}

// reviewBurn2MintRequests reviews burn2mint requests
func reviewBurn2MintRequests() {
	showMessage("Reviewing Burn2Mint requests...")
	// Implementation would go here
}

// manageStake manages stake
func manageStake() {
	showMessage("Managing stake...")
	// Implementation would go here
}

// updateENindexKeys updates ENindex keys
func updateENindexKeys() {
	showMessage("Updating ENindex keys...")
	// Implementation would go here
}

// showBurn2MintMenu displays the burn2mint menu
func (app *AppState) showBurn2MintMenu() {
	if !appState.isWalletRunning {
		showMessage("Wallet RPC is not running")
		return
	}

	menuItems := []struct {
		text   string
		action func()
	}{
		{"Small Burn (0.8 " + CurrentConfig.CoinName + ")", func() { startBurnProcess(CurrentConfig.BurnSmallAmount) }},
		{"Large Burn (800 " + CurrentConfig.CoinName + ")", func() { startBurnProcess(CurrentConfig.BurnLargeAmount) }},
		{"Back to Main Menu", func() { appState.pages.SwitchToPage("main") }},
	}

	// Create list for menu items
	list := tview.NewList().
		SetMainTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedTextColor(tview.Styles.PrimaryTextColor).
		SetSelectedBackgroundColor(tview.Styles.MoreContrastBackgroundColor)

	for _, item := range menuItems {
		list.AddItem(item.text, "", 0, item.action)
	}

	// Create layout
	title := tview.NewTextView().
		SetText("🔥➡️💎 Burn2Mint Menu").
		SetTextColor(tview.Styles.PrimaryTextColor).
		SetTextAlign(tview.AlignCenter)

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(title, 3, 0, false).
		AddItem(list, 0, 1, true)

	appState.pages.AddPage("burn2mint", layout, true, true)
	appState.pages.SwitchToPage("burn2mint")
}

// startBurnProcess starts the burn process
func startBurnProcess(amount int64) {
	// Show confirmation dialog
	message := fmt.Sprintf("Are you sure you want to burn %f %s?\nThis action cannot be undone.",
		float64(amount)/float64(CurrentConfig.CoinUnits), CurrentConfig.CoinName)

	modal := tview.NewModal().
		SetText(message).
		AddButtons([]string{"Confirm", "Cancel"}).
		SetDoneFunc(func(buttonIndex int, buttonLabel string) {
			if buttonLabel == "Confirm" {
				// Perform burn
				performBurn(amount)
			} else {
				appState.pages.SwitchToPage("burn2mint")
			}
		})

	appState.pages.AddPage("burnConfirm", modal, true, true)
	appState.pages.SwitchToPage("burnConfirm")
}

// performBurn performs the actual burn
func performBurn(amount int64) {
	params := map[string]interface{}{"amount": amount}

	result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.CreateBurnRPC, params)
	if err != nil {
		showMessage("Error creating burn: " + err.Error())
		return
	}

	txHash, ok := result["tx_hash"]
	if !ok {
		showMessage("Unexpected response format")
		return
	}

	// Show progress
	progressMessage := fmt.Sprintf("🔥 Burn Transaction Created\nTX Hash: %s\n\nWaiting for confirmations...", txHash)
	showProgress(progressMessage, 10, func() {
		// Request consensus after confirmations
		requestConsensus(fmt.Sprintf("%v", txHash), amount)
	})
}

// requestConsensus requests consensus for a burn
func requestConsensus(txHash string, amount int64) {
	progressMessage := "👑 Requesting Elderfier Consensus...\nThis may take a few moments."
	showProgress(progressMessage, 5, func() {
		params := map[string]interface{}{
			"tx_hash": txHash,
			"amount":  amount,
		}

		result, err := walletRpcCall(CurrentConfig.WalletRPCPort, CurrentConfig.RequestConsensusRPC, params)
		if err != nil {
			showMessage("Error requesting consensus: " + err.Error())
			return
		}

		proof, ok := result["eldernode_proof"]
		if !ok {
			showMessage("Unexpected response format")
			return
		}

		// Check if xfg-stark is available
		if _, err := exec.LookPath("xfg-stark"); err != nil {
			message := fmt.Sprintf("✅ Consensus Received!\n\nEldernode Proof: %v\n\nNext steps:\n1. Install xfg-stark CLI\n2. Run: xfg-stark generate-proof --tx-hash %s --amount %d --eldernode-proof %v",
				proof, txHash, amount, proof)
			showMessage(message)
		} else {
			// Generate STARK proof
			generateStarkProof(txHash, amount, fmt.Sprintf("%v", proof))
		}
	})
}

// generateStarkProof generates a STARK proof
func generateStarkProof(txHash string, amount int64, proof string) {
	progressMessage := "🔐 Generating STARK Proof...\nUsing xfg-stark CLI."
	showProgress(progressMessage, 10, func() {
		xfgStarkPath := binPath("xfg-stark")
		if xfgStarkPath == "" {
			xfgStarkPath = "xfg-stark"
		}

		cmd := exec.Command(xfgStarkPath,
			"generate-proof",
			"--tx-hash", txHash,
			"--amount", fmt.Sprintf("%d", amount),
			"--eldernode-proof", proof)

		output, err := cmd.CombinedOutput()
		if err != nil {
			showMessage(fmt.Sprintf("Error generating STARK proof: %s\nOutput: %s", err.Error(), string(output)))
			return
		}

		message := fmt.Sprintf("🎉 STARK Proof Generated Successfully!\n\nOutput: %s\n\nNext steps:\n1. Submit to Arbitrum L2\n2. Receive HEAT on Ethereum L1", string(output))
		showMessage(message)
	})
}

// showLogs displays the application logs
func showLogs() {
	// Force update the log text to ensure we see the latest logs
	logText := strings.Join(appState.logs, "\n")
	if logText == "" {
		logText = "No logs available. Try starting node or wallet to see logs."
		appState.logs = append(appState.logs, "[INFO] Logs screen opened - no logs yet")
		logText = strings.Join(appState.logs, "\n")
	}

	textView := tview.NewTextView().
		SetText(logText).
		SetScrollable(true).
		SetWrap(true)

	textView.SetBorder(true).SetTitle("Application Logs").SetTitleAlign(tview.AlignLeft)

	// Create layout with back button
	backButton := tview.NewButton("Back to Main Menu").SetSelectedFunc(func() {
		appState.pages.SwitchToPage("main")
	})

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(textView, 0, 1, true).
		AddItem(backButton, 3, 0, true)

	// Handle ESC key to exit to main menu
	layout.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		if event.Key() == tcell.KeyEsc {
			appState.pages.SwitchToPage("main")
			return nil
		}
		return event
	})

	appState.pages.AddPage("logs", layout, true, true)
	appState.pages.SwitchToPage("logs")
}

// showProgress shows a progress indicator
func showProgress(message string, steps int, onComplete func()) {
	// Use a TextView to simulate progress since tview doesn't have ProgressBar
	progressText := tview.NewTextView().
		SetTextAlign(tview.AlignCenter).
		SetDynamicColors(true)

	textView := tview.NewTextView().
		SetText(message).
		SetTextAlign(tview.AlignCenter)

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(textView, 3, 0, false).
		AddItem(progressText, 1, 0, false)

	appState.pages.AddPage("progress", layout, true, true)
	appState.pages.SwitchToPage("progress")

	// Simulate progress
	go func() {
		for i := 0; i <= steps; i++ {
			time.Sleep(500 * time.Millisecond)
			appState.app.QueueUpdateDraw(func() {
				// Create a simple progress bar using characters
				progressBar := ""
				for j := 0; j < steps; j++ {
					if j < i {
						progressBar += "█"
					} else {
						progressBar += "░"
					}
				}
				progressText.SetText(fmt.Sprintf("[%d/%d] %s", i, steps, progressBar))
			})
		}

		appState.app.QueueUpdateDraw(func() {
			appState.pages.RemovePage("progress")
			if onComplete != nil {
				onComplete()
			}
		})
	}()
}

// showMessage shows a message dialog
func showMessage(message string) {
	modal := tview.NewModal().
		SetText(message).
		AddButtons([]string{"OK"}).
		SetDoneFunc(func(buttonIndex int, buttonLabel string) {
			appState.pages.SwitchToPage("main")
		})

	appState.pages.AddPage("message", modal, true, true)
	appState.pages.SwitchToPage("message")
}

// streamPipe handles streaming output from a pipe like the original TUI
func streamPipe(r io.Reader, prefix string, appState *AppState) {
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
				appState.app.QueueUpdateDraw(func() {
					appState.logs = append(appState.logs, fmt.Sprintf("%s: %s", prefix, line))
				})
			}
		}
		if err != nil {
			if err != io.EOF {
				appState.app.QueueUpdateDraw(func() {
					appState.logs = append(appState.logs, fmt.Sprintf("%s stream error: %s", prefix, err.Error()))
				})
			}
			return
		}
	}
}

// binPath finds the binary path
func binPath(binName string) string {
	// Check in various build directories - expanded list
	buildPaths := []string{
		filepath.Join("..", "build", "src", binName),
		filepath.Join("..", "build", "release", "src", binName),
		filepath.Join("..", "build-test", "src", binName),
		filepath.Join("..", "bulid3", "release", "src", binName),
		filepath.Join("build", "src", binName),
		filepath.Join("build", "release", "src", binName),
		filepath.Join("..", "..", "build", "src", binName), // In case we're in tui/build
		filepath.Join("..", "..", "build", "release", "src", binName),
		filepath.Join("/home/ar/fuego", "build", "release", "src", binName),
		filepath.Join("/home/ar/fuego", "build", "src", binName),
		filepath.Join("/home/ar/fuego", "bulid3", "release", "src", binName),
	}

	for _, buildPath := range buildPaths {
		if _, err := os.Stat(buildPath); err == nil {
			appState.app.QueueUpdateDraw(func() {
				appState.logs = append(appState.logs, fmt.Sprintf("[DEBUG] Found %s at: %s", binName, buildPath))
			})
			return buildPath
		}
	}

	// Check in PATH
	if path, err := exec.LookPath(binName); err == nil {
		appState.app.QueueUpdateDraw(func() {
			appState.logs = append(appState.logs, fmt.Sprintf("[DEBUG] Found %s in PATH: %s", binName, path))
		})
		return path
	}

	appState.app.QueueUpdateDraw(func() {
		appState.logs = append(appState.logs, fmt.Sprintf("[ERROR] Could not find binary: %s", binName))
	})
	return ""
}

// getNodeInfo gets node information
func getNodeInfo() (*NodeInfo, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/get_info", CurrentConfig.NodeRPCPort)

	// Create HTTP client with timeout to prevent freezing
	client := &http.Client{
		Timeout: 5 * time.Second,
	}

	resp, err := client.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}

	var rpcResp RPCResponse
	if err := json.NewDecoder(resp.Body).Decode(&rpcResp); err != nil {
		return nil, err
	}

	if rpcResp.Error != nil {
		return nil, fmt.Errorf("RPC error: %s", rpcResp.Error.Message)
	}

	resultBytes, err := json.Marshal(rpcResp.Result)
	if err != nil {
		return nil, err
	}

	var info NodeInfo
	if err := json.Unmarshal(resultBytes, &info); err != nil {
		return nil, err
	}

	return &info, nil
}

// walletRpcCall makes an RPC call to the wallet
func walletRpcCall(port int, method string, params interface{}) (map[string]interface{}, error) {
	url := fmt.Sprintf("http://127.0.0.1:%d/json_rpc", port)

	// Create HTTP client with timeout to prevent freezing
	client := &http.Client{
		Timeout: 5 * time.Second,
	}

	request := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      "tui",
		"method":  method,
	}

	if params != nil {
		request["params"] = params
	}

	jsonData, err := json.Marshal(request)
	if err != nil {
		return nil, err
	}

	resp, err := client.Post(url, "application/json", strings.NewReader(string(jsonData)))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}

	var rpcResp RPCResponse
	if err := json.NewDecoder(resp.Body).Decode(&rpcResp); err != nil {
		return nil, err
	}

	if rpcResp.Error != nil {
		return nil, fmt.Errorf("RPC error: %s", rpcResp.Error.Message)
	}

	result, ok := rpcResp.Result.(map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("unexpected result format")
	}

	return result, nil
}

// logStream logs output from a stream
func logStream(stream interface{}, prefix string) {
	// Convert stream to io.Reader
	var reader io.Reader
	switch s := stream.(type) {
	case io.Reader:
		reader = s
	case *os.File:
		reader = s
	default:
		appState.app.QueueUpdateDraw(func() {
			appState.logs = append(appState.logs, fmt.Sprintf("[%s] Invalid stream type", prefix))
		})
		return
	}

	// Read from the stream and add to logs
	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		line := scanner.Text()
		if line != "" {
			appState.app.QueueUpdateDraw(func() {
				appState.logs = append(appState.logs, fmt.Sprintf("[%s] %s", prefix, line))
				// Keep only the last 1000 log entries to prevent memory issues
				if len(appState.logs) > 1000 {
					appState.logs = appState.logs[len(appState.logs)-1000:]
				}
			})
		}
	}

	if err := scanner.Err(); err != nil {
		appState.app.QueueUpdateDraw(func() {
			appState.logs = append(appState.logs, fmt.Sprintf("[%s] Stream error: %v", prefix, err))
		})
	}
}
