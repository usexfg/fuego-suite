# Fuego Pre-Production Checklist

**Status:** living document  
**Audience:** maintainers preparing a mainnet or public testnet release  
**Last updated:** 2026-07-20  

Green `make` is **necessary but not sufficient**. This checklist gates enabling money-moving or network-visible features (orderbook, limit deposits, atomic swaps, HEAT mint paths).

**Privacy law (non-negotiable):** Fuego is a privacy chain. Do **not** put spend keys, view keys, mnemonics, or address-identifying material in `tx_extra`, logs, or public RPC. Prefer commitments / hashes / signatures under existing key models — never plaintext keys on-chain.

---

## How to use

1. Copy the release section into the release PR or tag notes.
2. Mark each item `[x]` only with evidence (CI URL, test command output, review link, config snippet).
3. Features that are **code-complete but not activated** still need gate confirmation (hardfork height / compile flag / config default off).
4. Any **Blocker** left open = no production enablement for that feature.

**Severity legend**

| Tag | Meaning |
|-----|---------|
| **Blocker** | Must pass before enabling the feature on mainnet |
| **Release** | Must pass before a tagged public binary |
| **Ops** | Must pass for how you run production nodes |
| **Follow-up** | Tracked debt; may ship disabled |

---

## 0. Build & packaging

- [ ] **Release** `make -j$(nproc)` exits 0 on the release commit
- [ ] **Release** Binaries present: `fuegod`, `fire_wallet` (and intended TUI/swapxfg if shipped)
- [ ] **Release** CI green on intended matrix (Ubuntu 22/24, and any other platform you ship):
  - [ ] `check.yml` / ubuntu workflows
  - [ ] macOS / Windows if those artifacts are published
  - [ ] Docker image build if you publish images
- [ ] **Release** Submodule `external/secp256k1` pinned and matches CI
- [ ] **Release** Boost 1.86+ documented for builders; release notes mention dependency bounds
- [ ] **Release** Version / network magic / genesis match intended network (mainnet vs testnet)

---

## 1. Feature gates (what can peers already do?)

For each feature, record: **disabled by default / hardfork height / always on**.

| Feature | Gate (height / flag / config) | Default in this release | Ship enabled? |
|---------|-------------------------------|-------------------------|---------------|
| Hearth limit deposits / withdraw | | | [ ] |
| v2 P2P orderbook (open/cancel/fill/reserve) | | | [ ] |
| Daemon RPC `/placeorder` `/cancelorder` | | | [ ] |
| Atomic swap daemon / public swap RPC | | | [ ] |
| AFK lock / claim paths | | | [ ] |
| HEAT mint / market auth tags | | | [ ] |
| SPV / multi-chain clients in default binary | | | [ ] |

- [ ] **Blocker** No money feature is “live by accident” (peer messages accepted and applied without intentional activation)
- [ ] **Blocker** Upgrade path documented if activation is height-based
- [ ] **Release** CHANGELOG / release notes list enabled vs disabled features honestly

---

## 2. Security residuals (orderbook / limits / swaps)

Track findings that remain after the 2026-07 security fix pass. Re-verify before enablement.

### 2.1 Limit deposits & withdraw

- [ ] **Blocker** Withdraw validation enforces deposit exists, not withdrawn, not expired
- [ ] **Blocker** Conservation: net extra XFG/HEAT out cannot exceed deposited amount (no unbacked mint)
- [ ] **Blocker** Ownership model documented: **no plaintext spend/view in `tx_extra`**
- [ ] **Blocker** If withdraw is still “orderId + fee tx” without spend-proof: either  
  - **keep feature off on mainnet**, or  
  - implement privacy-preserving ownership (e.g. spend of deposit commitment / ring-compatible proof) — **not** keys-in-extra
- [ ] **Release** Tests cover: unknown orderId reject, double-withdraw reject, over-claim amount reject

### 2.2 P2P orderbook

- [ ] **Blocker** `pair` always bounds-checked before `m_orderBooks[]` (open, match, cancel, snapshot)
- [ ] **Blocker** Order open requires valid signature; `orderId` matches canonical encoding
- [ ] **Blocker** Cancel requires signature over `"cancel:"+orderId` (P2P + RPC)
- [ ] **Blocker** Tombstones prevent re-open of filled/cancelled orderIds
- [ ] **Blocker** Fill replay keys full fill identity (not maker-only)
- [ ] **Follow-up** Reserve: taker authentication still weak — do not rely on reserve alone for safety-critical matching until signed reserve exists
- [ ] **Release** Reserve-ack maker signature verified; expired reservations free reserved amount
- [ ] **Release** Tests: OOB pair rejected, bad sig rejected, cancel grief without sig fails, replay rejected

### 2.3 Daemon RPC surface

- [ ] **Blocker** `/placeorder` rejects unsigned orders; requires orderId + makerPubKey + signature + nonce
- [ ] **Blocker** `/cancelorder` rejects unsigned cancels
- [ ] **Ops** Restricted RPC mode on for public nodes (or bind localhost + firewall)
- [ ] **Ops** Wallet RPC never exposed publicly without auth
- [ ] **Blocker** Inventory all swap/order endpoints (`/initiate`, `/accept`, `/processswap`, `/refundswap`, …):  
  - private network / auth / or disabled in production config
- [ ] **Release** No RPC returns spend keys, view keys, or mnemonics on any public interface

### 2.4 Wallet / AFK / swaps

- [ ] **Blocker** `sign_order` and daemon validation use the **same** canonical bytes (no padding mismatch)
- [ ] **Blocker** AFK claim requires real `payout_address` (no placeholders)
- [ ] **Follow-up** AFK claim path that spends the **lock UTXO** with extracted secret (not only wallet-balance send) before relying on AFK in production
- [ ] **Release** Adaptor secrets encrypted at rest; no logging of preimages/secrets
- [ ] **Release** Cross-chain claim paths reviewed for wrong-address / wrong-chain bugs per asset (XMR, BCH, EVM, …)

### 2.5 Privacy & leakage

- [ ] **Blocker** Grep release build logs for accidental key dumps (sample mainnet-like run)
- [ ] **Blocker** No new `tx_extra` fields that deanonymize depositors (keys, plain addresses as identity)
- [ ] **Release** Explorer / public APIs reviewed for over-sharing
- [ ] **Release** Default log level does not print sensitive RPC bodies

---

## 3. Tests (must run, not only compile)

### 3.1 Automated (release commit)

- [ ] **Release** `make -j$(nproc)` clean
- [ ] **Release** Run orderbook-related binaries (from `build/release/src/` as applicable):
  - [ ] `test_orderbook` (and phase tests if enabled for this release)
  - [ ] New/updated tests for signed place/cancel, pair bounds, withdraw conservation (add if missing — **Blocker** for enabling those features)
- [ ] **Release** SPV / swap unit tests relevant to shipped chains:
  - [ ] `test_spv_merkle`, `test_spv_headers`, `test_neutrino` (if SPV is shipped)
  - [ ] adaptor / HTLC tests for enabled chains
- [ ] **Release** `make build-with-tests` or UnitTests suite if this tag claims full test coverage
- [ ] **Release** Go: `cd swapxfg && go test ./...` (and `tui` if tested)

### 3.2 Manual / integration smoke

- [ ] **Ops** Fresh datadir sync to tip (or snapshot restore) on intended network
- [ ] **Ops** Wallet create / open / transfer smoke on testnet
- [ ] **Blocker** (if orderbook enabled) Signed place → book shows depth → signed cancel
- [ ] **Blocker** (if swaps enabled) One full swap happy path + one refund/timeout path per enabled pair class
- [ ] **Ops** Restart daemon mid-sync / mid-swap; confirm no corruption

---

## 4. Cryptographic / consensus review

- [ ] **Blocker** Internal second pass on residuals (section 2) with written notes
- [ ] **Blocker** (mainnet money features) Independent human review of:
  - [ ] Consensus validation for any new tx types / extra tags
  - [ ] Orderbook matching + reservation economics
  - [ ] HEAT mint conservation
  - [ ] Adaptor-sig claim / refund safety
- [ ] **Release** Review notes linked from release PR (even if “no issues found”)
- [ ] **Follow-up** Fuzz Levin order messages + `tx_extra` parsers (track issue; not always release-blocking)

---

## 5. Operations & deployment

### 5.1 Node configuration

- [ ] **Ops** Production sample config: P2P ports, RPC bind address, restricted RPC
- [ ] **Ops** Firewall: public P2P open; RPC/wallet closed or authenticated
- [ ] **Ops** Data directory backups / prune policy documented
- [ ] **Ops** Disk / memory expectations for full node documented
- [ ] **Ops** Seed nodes / DNS / bootstrap list correct for network

### 5.2 Wallet & keys

- [ ] **Ops** Users warned: backup seed offline; never paste seed into AI/chat/RPC logs
- [ ] **Ops** Wallet RPC password required; default bind not `0.0.0.0`
- [ ] **Ops** No production automation stores spend keys in plaintext env files committed to git

### 5.3 Monitoring

- [ ] **Ops** Health: height, peer count, disk, RPC latency
- [ ] **Ops** Alerts for stuck height / peer collapse
- [ ] **Ops** (if swaps) Monitor failed swaps / refund backlog without logging secrets

### 5.4 Release artifacts

- [ ] **Release** Checksums / signatures for published binaries
- [ ] **Release** Reproducible or at least documented build environment
- [ ] **Release** Downgrade / rollback story if activation goes wrong

---

## 6. CI / platform matrix

- [ ] **Release** Ubuntu 22.04 build
- [ ] **Release** Ubuntu 24.04 build
- [ ] **Release** macOS (if shipping)
- [ ] **Release** Windows (if shipping)
- [ ] **Release** ARM64 / RPi (if shipping)
- [ ] **Release** Termux / AppImage (if shipping)
- [ ] **Ops** Install `gh` or use GitHub MCP to confirm Actions on the release tag

---

## 7. Testnet soak (before mainnet enablement)

Minimum soak for any new money feature:

- [ ] **Blocker** ≥ 72h multi-node testnet (or documented equivalent)
- [ ] **Blocker** Include adversarial cases: bad order sigs, cancel grief attempts, withdraw over-claim, RPC abuse
- [ ] **Ops** One coordinated restart of all nodes
- [ ] **Ops** Capture and review logs for secret leakage
- [ ] **Release** Soak report linked from release notes

---

## 8. Documentation & disclosure

- [ ] **Release** Release notes: features on/off, migration steps, known issues
- [ ] **Release** Risk warnings updated (`docs/security/risk-warnings.mdx` if user-facing)
- [ ] **Release** Wallet safety docs match real RPC behavior
- [ ] **Ops** Security contact / vulnerability reporting path is real (`docs/SECURITY.md` or project policy)
- [ ] **Follow-up** Public post-mortem template ready if activation incident occurs

---

## 9. Go / no-go decision

Complete only at the end:

| Question | Answer |
|----------|--------|
| Release commit SHA | |
| Networks in scope (mainnet / testnet only) | |
| Features enabled on this network | |
| All **Blocker** items closed? | [ ] Yes |
| External review required and done? | [ ] Yes / [ ] N/A (justify) |
| Rollback plan | |
| Sign-off (name / date) | |

**Rule:** If any **Blocker** is open for an enabled feature → **NO-GO** for that feature (disable gate or delay release).

---

## 10. Quick commands (evidence helpers)

```bash
# Build
make -j$(nproc)

# Spot binaries
ls -la build/release/src/fuegod build/release/src/fire_wallet

# Example unit binaries (paths may vary)
ls build/release/src/test_*

# Go clients
(cd swapxfg && go test ./...)
(cd tui && go test ./... 2>/dev/null || true)

# CI (requires gh)
gh run list --branch master --limit 10
```

Privacy-safe log skim (adjust paths):

```bash
# Should return nothing sensitive
grep -RInE 'spendSecret|viewSecret|mnemonic|private_key|seed phrase' build/ 2>/dev/null | head
```

---

## Related docs

- Architecture / agent notes: `AGENTS.md`, `CLAUDE.md`
- Prior swap/security plans: `docs/review/`
- User-facing security: `docs/security/risk-warnings.mdx`, `docs/security/wallet-safety.mdx`
- Build skill for agents: `.claude/skills/fuego-build/`
- Crypto edit gate: `.claude/skills/crypto-change-gate/`

---

## Changelog of this checklist

| Date | Note |
|------|------|
| 2026-07-20 | Initial draft after green `make`, orderbook/RPC security fixes, and residual-risk triage |
