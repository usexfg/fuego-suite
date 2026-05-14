// swapxfg/app/bridge_eth.go
// ETH bridge page + Go helper methods.
package app

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"math/big"
	"strings"

	"github.com/ethereum/go-ethereum/accounts/abi"
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
<script src="https://cdn.jsdelivr.net/npm/ethers@5.7.2/dist/ethers.umd.min.js"
        type="application/javascript"
        integrity="sha384-Htz1SE4Sl5aitpvFgr2j0sfsGUIuSXI6t8hEyrlQ93zflEF3a29bH2AvkUROUw7J"
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
func buildHTLCLockCalldata(hashlockHex, timeoutHex string) (string, error) {
	hashlockHex = strings.TrimPrefix(hashlockHex, "0x")
	timeoutHex = strings.TrimPrefix(timeoutHex, "0x")

	if len(hashlockHex) != 64 {
		return "", fmt.Errorf("buildHTLCLockCalldata: hashlock must be 64 hex chars (bytes32), got %d", len(hashlockHex))
	}

	hashlockBytes, err := hex.DecodeString(hashlockHex)
	if err != nil {
		return "", fmt.Errorf("buildHTLCLockCalldata: invalid hashlock hex: %v", err)
	}

	var hashlock [32]byte
	copy(hashlock[:], hashlockBytes)

	timeoutBig := new(big.Int)
	timeoutBig, ok := timeoutBig.SetString(timeoutHex, 16)
	if !ok {
		return "", fmt.Errorf("buildHTLCLockCalldata: invalid timeout hex")
	}

	parsedABI, err := abi.JSON(strings.NewReader(`[{"constant":false,"inputs":[{"name":"_hashlock","type":"bytes32"},{"name":"_timeout","type":"uint256"}],"name":"lock","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"}]`))
	if err != nil {
		return "", fmt.Errorf("buildHTLCLockCalldata: failed to parse ABI: %v", err)
	}

	data, err := parsedABI.Pack("lock", hashlock, timeoutBig)
	if err != nil {
		return "", fmt.Errorf("buildHTLCLockCalldata: failed to pack calldata: %v", err)
	}

	return "0x" + hex.EncodeToString(data), nil
}
