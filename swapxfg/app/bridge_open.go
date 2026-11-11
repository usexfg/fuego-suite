// swapxfg/app/bridge_open.go
// OS-specific URL opener for the bridge server.
package app

import (
	"os/exec"
	"runtime"
)

func openURLOS(url string) error {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "linux":
		cmd = exec.Command("xdg-open", url)
	case "darwin":
		cmd = exec.Command("open", url)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		return nil // best effort
	}
	return cmd.Start() // non-blocking
}
