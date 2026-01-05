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
}

func main() {
	// Detect build mode from executable name or argument
	execName := filepath.Base(os.Args[0])
	
	if strings.Contains(execName, "testnet") {
		CurrentConfig = TestnetConfig
	} else {
		CurrentConfig = MainnetConfig
	}

	// Also allow runtime override with --testnet flag
	if len(os.Args) > 1 && os.Args[1] == "--testnet" {
		CurrentConfig = TestnetConfig
	}

	initVersionInfo()

	tui := &Tui{
		app:  tview.NewApplication(),
		logs: []string{fmt.Sprintf("🔥 %s TUI Ready - %s", CurrentConfig.Name, verInfo.fullVersion)},
	}

	tui.app.EnableMouse(true)
	tui.setupUI()

	if err := tui.app.Run(); err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}
}

func initVersionInfo() {
	versionFile := filepath.Join(CurrentConfig.HomeDir, "build", "release", "version", "version.h")
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
		verInfo.testnetVersion = CurrentConfig.Name + " v." + verInfo.projectVersion
		verInfo.fullVersion = fmt.Sprintf("%s %s", CurrentConfig.CoinName, CurrentConfig.Name)
	} else {
		verInfo.fullVersion = fmt.Sprintf("%s %s (dev)", CurrentConfig.CoinName, CurrentConfig.Name)
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
		SetText(fmt.Sprintf("[yellow]🔥 %s %s TUI\n[green]%s\n[grey]Mouse enabled - Click items or use shortcuts", 
			CurrentConfig.CoinName, CurrentConfig.Name, verInfo.fullVersion))

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
	list.AddItem("Æternal Flame", "Burn2Mint flow (XFG→HEAT)", '3', func() {
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
				text += fmt.Sprintf("[white]Wallet: [green]running[white] on port [cyan]%d\n", CurrentConfig.WalletRPCPort)
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
		SetTitle(fmt.Sprintf("[yellow]Node & Wallet Controls (%s)", CurrentConfig.Name)).
		SetBorder(true)

	list.AddItem("Start Node", fmt.Sprintf("Launch %s on ports %d/%d", CurrentConfig.NodeBinary, CurrentConfig.NodeRPCPort, CurrentConfig.NodeP2PPort), '1', t.startNode)
	list.AddItem("Stop Node", "Kill running node process", '2', t.stopNode)
	list.AddItem("Node Status", "Query height and peer count", '3', t.nodeStatus)
	list.AddItem("Start Wallet RPC", fmt.Sprintf("Launch %s on port %d", CurrentConfig.WalletBinary, CurrentConfig.WalletRPCPort), '4', t.startWallet)
	list.AddItem("[gray]Back", "Return to main menu", '0', func() {
		t.pages.RemovePage("nodeload")
	})

	t.addLogViewAndBack(list, "nodeload")
}

func (t *Tui) showElderfierMenu() {
	list := tview.NewList().
		SetTitle(fmt.Sprintf("[cyan]Elderfier Menu (%s)", CurrentConfig.Name)).
		SetBorder(true)

	minStake := float64(CurrentConfig.MinStake) / 100000000
	list.AddItem("Start Elderfier Stayking", fmt.Sprintf("Create %.0f %s stake deposit", minStake, CurrentConfig.CoinName), '1', t.startStake)
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

func (t *Tui) showBurn