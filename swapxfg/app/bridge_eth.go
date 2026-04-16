// swapxfg/app/bridge_eth.go
// ETH bridge page + Go helper methods.
package app

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"strings"
)

// ethBridgeHTML returns the inline HTML for the ETH MetaMask bridge page.
// The page connects back to the bridge WebSocket and handles eth_* actions.
func ethBridgeHTML(port int) string {
	return fmt.Sprintf(`<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>swapxfg ETH bridge</title></head>
<body style="background:#111;color:#eee;font-family:monospace;padding:24px">
<h2 style="color:#FF5500">&#x26B3;&#xFE0F; swapxfg &middot; ETH bridge</h2>
<p id="status">Connecting to swapxfg&hellip;</p>
<script src="https://cdn.ethers.io/lib/ethers-5.7.2.umd.min.js"
        type="application/javascript"
        integrity="sha384-KiZhooPaHFaFiXrJzCLPkiV6FwP5e3T1KxCPq0EAK5q6d2MkiLfYuA5KBqALqcX"
        crossorigin="anonymous"></script>
<script>
const ws = new WebSocket("ws://127.0.0.1:%d/bridge/ws");
const st = document.getElementById("status");

ws.onopen = () => { st.textContent = "Connected to swapxfg. Waiting for MetaMask..."; };
ws.onclose = () => { st.textContent = "swapxfg disconnected."; };

ws.onmessage = async (ev) => {
  const req = JSON.parse(ev.data);
  let result = null, error = null;
  try {
    if (!window.ethereum) throw new Error("MetaMask not found");
    const provider = new ethers.providers.Web3Provider(window.ethereum);
    await provider.send("eth_requestAccounts", []);
    const signer = provider.getSigner();

    switch (req.action) {
      case "eth_getAddress":
        result = await signer.getAddress();
        break;
      case "eth_getBalance": {
        const addr = req.params.address || (await signer.getAddress());
        const bal = await provider.getBalance(addr);
        result = bal.toString();
        break;
      }
      case "eth_sendTx": {
        const tx = await signer.sendTransaction({
          to:    req.params.to,
          value: ethers.BigNumber.from(req.params.value || "0"),
          data:  req.params.data || "0x",
        });
        result = tx.hash;
        break;
      }
      case "erc20_getBalance": {
        const abi = ["function balanceOf(address) view returns (uint256)"];
        const c = new ethers.Contract(req.params.token, abi, provider);
        result = (await c.balanceOf(req.params.address)).toString();
        break;
      }
      case "erc20_approve": {
        const abi = ["function approve(address,uint256) returns (bool)"];
        const c = new ethers.Contract(req.params.token, abi, signer);
        const tx = await c.approve(req.params.spender, req.params.amount);
        result = tx.hash;
        break;
      }
      case "erc20_transfer": {
        const abi = ["function transfer(address,uint256) returns (bool)"];
        const c = new ethers.Contract(req.params.token, abi, signer);
        const tx = await c.transfer(req.params.to, req.params.amount);
        result = tx.hash;
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

// ethAllowedActions is the whitelist of action strings the ETH bridge will
// accept. Any action not in this map is rejected before being sent to the
// browser, preventing injection of unsanctioned operations.
var ethAllowedActions = map[string]bool{
	"eth_getAddress":   true,
	"eth_getBalance":   true,
	"eth_sendTx":       true,
	"erc20_getBalance": true,
	"erc20_approve":    true,
	"erc20_transfer":   true,
}

// sendEth sends a bridge request after validating the action against the ETH
// whitelist.
func (b *BridgeServer) sendEth(req BridgeRequest) (BridgeResponse, error) {
	if !ethAllowedActions[req.Action] {
		return BridgeResponse{}, fmt.Errorf("bridge: unknown ETH action: %s", req.Action)
	}
	return b.Send(req)
}

// newReqID returns a cryptographically random 8-byte hex string for use as a
// JSON-RPC request ID. Using crypto/rand avoids the predictability of a
// monotonic counter, which can leak timing and ordering information.
func newReqID() string {
	b := make([]byte, 8)
	if _, err := rand.Read(b); err != nil {
		// crypto/rand failure is fatal on any sane OS; panic with context.
		panic("swapxfg: crypto/rand unavailable: " + err.Error())
	}
	return hex.EncodeToString(b)
}

// EthGetAddress retrieves the connected MetaMask wallet address.
func (b *BridgeServer) EthGetAddress() (string, error) {
	params, _ := json.Marshal(map[string]string{})
	resp, err := b.sendEth(BridgeRequest{ID: newReqID(), Action: "eth_getAddress", Params: params})
	if err != nil {
		return "", err
	}
	if resp.Error != "" {
		return "", fmt.Errorf("%s", resp.Error)
	}
	return resp.Result, nil
}

// EthGetBalance returns the ETH balance (in wei as string) for address.
func (b *BridgeServer) EthGetBalance(address string) (string, error) {
	params, _ := json.Marshal(map[string]string{"address": address})
	resp, err := b.sendEth(BridgeRequest{ID: newReqID(), Action: "eth_getBalance", Params: params})
	if err != nil {
		return "", err
	}
	if resp.Error != "" {
		return "", fmt.Errorf("%s", resp.Error)
	}
	return resp.Result, nil
}

// EthSendTransaction sends a raw ETH transaction via MetaMask. Returns tx hash.
func (b *BridgeServer) EthSendTransaction(to, value, data string) (string, error) {
	params, _ := json.Marshal(map[string]string{"to": to, "value": value, "data": data})
	resp, err := b.sendEth(BridgeRequest{ID: newReqID(), Action: "eth_sendTx", Params: params})
	if err != nil {
		return "", err
	}
	if resp.Error != "" {
		return "", fmt.Errorf("%s", resp.Error)
	}
	return resp.Result, nil
}

// Erc20Balance returns the ERC-20 token balance for address.
func (b *BridgeServer) Erc20Balance(token, address string) (string, error) {
	params, _ := json.Marshal(map[string]string{"token": token, "address": address})
	resp, err := b.sendEth(BridgeRequest{ID: newReqID(), Action: "erc20_getBalance", Params: params})
	if err != nil {
		return "", err
	}
	if resp.Error != "" {
		return "", fmt.Errorf("%s", resp.Error)
	}
	return resp.Result, nil
}

// OpenEthBridge opens the ETH bridge page in the default browser.
func (b *BridgeServer) OpenEthBridge() error {
	return openURL(b.EthURL())
}

// buildHTLCLockCalldata encodes calldata for an HTLC lock(bytes32,uint256) call.
//
// TODO: use go-ethereum/abi (github.com/ethereum/go-ethereum/accounts/abi) for
// proper ABI encoding once go-ethereum is added to go.mod without CGO issues.
// Until then we hand-assemble: 4-byte selector + 32-byte hashlock + 32-byte timeout.
//
// The function selector is keccak256("lock(bytes32,uint256)")[0:4] = 0x84cc9dfb.
// Both arguments are right-padded/left-padded to 32 bytes per ABI spec:
//   - bytes32 is already 32 bytes (passed as 64 hex chars, zero-right-padded)
//   - uint256 is left-padded to 32 bytes (big-endian)
//
// Returns an error if hashlock is not 64 hex chars or timeout is not ≤ 64 hex chars.
func buildHTLCLockCalldata(hashlockHex, timeoutHex string) (string, error) {
	// Strip optional 0x prefixes
	hashlockHex = strings.TrimPrefix(hashlockHex, "0x")
	timeoutHex = strings.TrimPrefix(timeoutHex, "0x")

	if len(hashlockHex) != 64 {
		return "", fmt.Errorf("buildHTLCLockCalldata: hashlock must be 64 hex chars (bytes32), got %d", len(hashlockHex))
	}
	if len(timeoutHex) == 0 || len(timeoutHex) > 64 {
		return "", fmt.Errorf("buildHTLCLockCalldata: timeout must be 1–64 hex chars (uint256), got %d", len(timeoutHex))
	}
	// Validate hex characters
	for _, c := range hashlockHex + timeoutHex {
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
			return "", fmt.Errorf("buildHTLCLockCalldata: non-hex character %q in input", c)
		}
	}

	// 4-byte selector for lock(bytes32,uint256): keccak256 = 0x84cc9dfb…
	const selector = "84cc9dfb"
	// bytes32 arg: already 32 bytes — no padding needed
	hashArg := hashlockHex
	// uint256 arg: left-pad with zeros to 64 hex chars (32 bytes)
	timeoutArg := fmt.Sprintf("%064s", timeoutHex)
	// Replace spaces with zeros (fmt.Sprintf pads with spaces for strings)
	timeoutArg = strings.ReplaceAll(timeoutArg, " ", "0")

	return "0x" + selector + hashArg + timeoutArg, nil
}
