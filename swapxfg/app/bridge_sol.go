// swapxfg/app/bridge_sol.go
// Solana bridge page + Go helper methods.
package app

import (
	"encoding/json"
	"fmt"
	"strconv"
)

// solBridgeHTML returns the inline HTML for the Solana Phantom bridge page.
func solBridgeHTML(port int) string {
	return fmt.Sprintf(`<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>swapxfg SOL bridge</title></head>
<body style="background:#111;color:#eee;font-family:monospace;padding:24px">
<h2 style="color:#FF5500">&#x26B3;&#xFE0F; swapxfg &middot; SOL bridge</h2>
<p id="status">Connecting to swapxfg&hellip;</p>
<script src="https://cdn.jsdelivr.net/npm/@solana/web3.js@1.95.3/lib/index.iife.min.js"
        integrity="sha384-xo1g+ODR6i1cxIVZeALc/apXFHeaJHO1j1whMO3dhtdBChWeaihnDs7UnXs9ch78"
        crossorigin="anonymous"></script>
<script>
const ws = new WebSocket("ws://127.0.0.1:%d/bridge/ws");
const st = document.getElementById("status");
const connection = new solanaWeb3.Connection(solanaWeb3.clusterApiUrl("mainnet-beta"));

ws.onopen = () => { st.textContent = "Connected to swapxfg. Waiting for Phantom..."; };
ws.onclose = () => { st.textContent = "swapxfg disconnected."; };

ws.onmessage = async (ev) => {
  const req = JSON.parse(ev.data);
  let result = null, error = null;
  try {
    const phantom = window.solana;
    if (!phantom || !phantom.isPhantom) throw new Error("Phantom wallet not found");
    await phantom.connect();

    switch (req.action) {
      case "sol_getAddress":
        result = phantom.publicKey.toString();
        break;
      case "sol_getBalance": {
        const pk = new solanaWeb3.PublicKey(req.params.pubkey || phantom.publicKey.toString());
        const lamports = await connection.getBalance(pk);
        result = lamports.toString();
        break;
      }
      case "sol_sendTx": {
        const txBytes = Uint8Array.from(atob(req.params.txBase64), c => c.charCodeAt(0));
        const tx = solanaWeb3.Transaction.from(txBytes);
        const signed = await phantom.signTransaction(tx);
        const sig = await connection.sendRawTransaction(signed.serialize());
        result = sig;
        break;
      }
      default:
        error = "unknown action: " + req.action;
    }
  } catch(e) {
    error = e.message;
  }
  ws.send(JSON.stringify({id: req.id, result: result ? String(result) : "", error: error || ""}));
  st.textContent = "Last: " + req.action + (error ? " ERROR: " + error : " OK: " + (result||""));
};
</script>
</body>
</html>`, port)
}

// solAllowedActions is the whitelist of action strings the SOL bridge will
// accept. Any action not in this map is rejected before being sent to the
// browser.
var solAllowedActions = map[string]bool{
	"sol_getAddress": true,
	"sol_getBalance": true,
	"sol_sendTx":     true,
}

// sendSol sends a bridge request after validating the action against the SOL
// whitelist.
func (b *BridgeServer) sendSol(req BridgeRequest) (BridgeResponse, error) {
	if !solAllowedActions[req.Action] {
		return BridgeResponse{}, fmt.Errorf("bridge: unknown SOL action: %s", req.Action)
	}
	return b.Send(req)
}

// SolGetBalance returns the SOL balance in lamports for the given pubkey.
func (b *BridgeServer) SolGetBalance(pubkey string) (uint64, error) {
	params, _ := json.Marshal(map[string]string{"pubkey": pubkey})
	resp, err := b.sendSol(BridgeRequest{ID: newReqID(), Action: "sol_getBalance", Params: params})
	if err != nil {
		return 0, err
	}
	if resp.Error != "" {
		return 0, fmt.Errorf("%s", resp.Error)
	}
	n, err := strconv.ParseUint(resp.Result, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("parse balance: %w", err)
	}
	return n, nil
}

// SolSendTransaction submits a base64-encoded Solana transaction via Phantom.
// Returns the transaction signature.
func (b *BridgeServer) SolSendTransaction(txBase64 string) (string, error) {
	params, _ := json.Marshal(map[string]string{"txBase64": txBase64})
	resp, err := b.sendSol(BridgeRequest{ID: newReqID(), Action: "sol_sendTx", Params: params})
	if err != nil {
		return "", err
	}
	if resp.Error != "" {
		return "", fmt.Errorf("%s", resp.Error)
	}
	return resp.Result, nil
}

// OpenSolBridge opens the SOL bridge page in the default browser.
func (b *BridgeServer) OpenSolBridge() error {
	return openURL(b.SolURL())
}
