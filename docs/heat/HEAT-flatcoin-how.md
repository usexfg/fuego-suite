# HEAT Flatcoin Implementation Plan

## Context

HEAT is Fuego's on-chain algorithmic floating-supply colored coin. Today HEAT operates under `HEAT_STABILITY_MODE = 2` (8:1 full float) — no CPI tracking, so HEAT preserves real value relative to XFG's market price but not against USD purchasing power.

The codebase already contains a **fully-implemented Mode 0** that targets a CPI-adjusted purchasing-power band (`$1.50–$2.50 × CPI_current / CPI_launch`) — but it is never invoked because nothing ever updates the CPI state. The flag `state.cpiOracleActive` defaults to `false`, the constants `HEAT_CPI_AUTO_INFLATION_BPS` and `HEAT_CPI_UPDATE_INTERVAL` are defined but unreferenced, and `state.cpiCurrentValue` never moves from its launch value of `100.0`.

This plan turns on the existing Mode 0 by wiring real CPI data into consensus state through a signed-attestation mechanism, then flipping `HEAT_STABILITY_MODE` from `2` to `0`. The result is the first **private flatcoin** — HEAT maintains constant purchasing power against USD, on-chain, with native privacy.

The companion doc `docs/design/HEAT_FLATCOIN_FORMULA.md` describes the *what* (formula, economic outcomes). This doc describes the *how* (file-by-file work, phasing, consensus model).

---

## What already exists (do not re-implement)

| Component | File | Status |
|---|---|---|
| Mode 0 CPI-aware band logic | `src/CryptoNoteCore/PiController.cpp:115–137` | ✅ Complete |
| `PiControllerState` CPI fields (`cpiLaunchValue`, `cpiCurrentValue`, `cpiUpdateHeight`, `cpiOracleActive`) | `src/CryptoNoteCore/PiController.h:48–51` | ✅ Defined + serialized |
| CPI constants (`HEAT_CPI_BASE_FLOOR`, `BASE_CEIL`, `LAUNCH_INDEX`, `SCALE`, `AUTO_INFLATION_BPS`, `UPDATE_INTERVAL`) | `src/CryptoNoteConfig.h:225–230` | ✅ Defined |
| PI controller integration with target_ratio | `src/CryptoNoteCore/Blockchain.cpp:3245–3284` | ✅ Mode 0 path will engage when `cpiOracleActive=true` |
| Hill damping disabled in Mode 0 (correct — Mode 0 uses bands, not damping) | `src/CryptoNoteCore/PiController.cpp:211` | ✅ Correct already |
| External price fetcher pattern (HTTPS + JSON + thread lifecycle) | `src/CryptoNoteCore/SwapOfferRelay.cpp:476–509` (`exbitronFetchThread`) | ✅ Reference pattern |

The **only** missing piece is the path from a real CPI feed into `state.cpiCurrentValue`.

---

## Core architectural decision: consensus on CPI value

CPI is external data — different nodes polling Truflation at different moments will see different values. Since `PiControllerState` is consensus state (persisted in blockchain, affects every HEAT mint validation), all nodes **must** agree on the same CPI value at the same height.

We resolve this with **signed CPI attestation transactions**:

1. A designated CPI signer (initially a single network-controlled key, federated later) polls Truflation + Pyth basket + BLS off-chain.
2. The signer computes median, signs an attestation: `(height_window, cpi_index_q64, source_count, timestamp)`.
3. The attestation is broadcast as a special transaction (new tx extra field, `TX_EXTRA_CPI_ATTESTATION = 0xC1`).
4. Block validators verify the signature against a hard-coded pubkey, check sanity bounds, and apply the update to `state.cpiCurrentValue`.
5. Nodes that don't see the attestation simply don't move CPI — peg behavior degrades gracefully (band tightens to last known value).

This keeps CPI **deterministic per height** (same tx → same state) while allowing the actual data source to evolve (Phase 2 federation, Phase 3 decentralized oracle network).

---

## Phases

### Phase 1 — CpiOracleService (off-chain, advisory) [3–4 days]

**Goal**: Build the fetcher infrastructure that produces a single trustworthy CPI value. No consensus impact yet — this just runs and exposes the value via local RPC for monitoring and Phase 2 signing.

**New files:**
- `src/CryptoNoteCore/CpiOracle/CpiOracleService.h`
- `src/CryptoNoteCore/CpiOracle/CpiOracleService.cpp`
- `src/CryptoNoteCore/CpiOracle/TruflationFetcher.cpp`
- `src/CryptoNoteCore/CpiOracle/PythBasketFetcher.cpp`
- `src/CryptoNoteCore/CpiOracle/BlsFetcher.cpp`
- `src/CryptoNoteCore/CpiOracle/FpiMarketFetcher.cpp` (optional 4th source)

**Pattern**: Mirror `exbitronFetchThread()` from `SwapOfferRelay.cpp:476–509`. Each fetcher is a `std::thread` polling its source, populating a per-source cache with `(value, updatedAt, sourceName)`.

**Sources to wire:**
| Source | Endpoint | Cadence | Normalization |
|---|---|---|---|
| Truflation | `https://api.truflation.com/values/current-inflation-rate` | hourly | Already in CPI-index form |
| Pyth basket | `https://hermes.pyth.network/v2/updates/price/latest?ids[]=...` × 4 feeds (gold, silver, oil, EUR) | 60s | Normalize basket vs launch-snapshot |
| BLS CPI-U | `https://api.bls.gov/publicAPI/v2/timeseries/data/CUUR0000SA0` | daily | Monthly publication, latest = current_CPI |
| FPI market price | Uniswap V3 FPI/USDC TWAP via on-chain `eth_call` | hourly | `fpiPrice × launchIndex` |

**Median selection in `CpiOracleService::getCurrentCpi()`:**
- Collect fresh samples (per-source freshness window: Truflation 24h, BLS 35d, Pyth 5min, FPI 1h).
- If samples is empty → return `launchIndex` (peg behaves as stablecoin).
- Otherwise → sort, return median.
- Report `(median, sourceCount, lastUpdate)` for monitoring.

**RPC endpoint to expose** (advisory, doesn't affect consensus yet):
- `GET /cpi` → JSON `{"value": 103.45, "sources": ["truflation","pyth_basket","bls"], "updated": 1716671234, "stale": false}`

**Wiring**: Instantiated and owned by `core` (CryptoNoteCore/Core.cpp). Started/stopped alongside other services. Survives daemon restart since only the cache is in-memory — data is re-fetched on startup.

**Phase 1 exit criteria**: `curl http://localhost:18180/cpi` returns a live median within 5 minutes of daemon start. No consensus state has been touched.

---

### Phase 2 — On-chain CPI attestation [4–5 days]

**Goal**: Get the off-chain CPI value into consensus state via a signed transaction, then flip Mode 0 on.

**Step 2A — New tx extra field**

Add to `src/CryptoNoteCore/TransactionExtra.h`:
```cpp
const uint8_t TX_EXTRA_CPI_ATTESTATION = 0xC1;

struct TransactionExtraCpiAttestation {
  uint64_t cpiIndexQ64;       // FixedPoint64 raw value
  uint64_t epochHeight;       // height at which this attestation is valid
  uint8_t  sourceCount;       // how many sources contributed
  uint64_t observationTime;   // unix timestamp (anti-replay)
  Crypto::Signature sig;      // signature over the above by m_cpiSignerKey
};
```

Serialization helpers parallel to existing `TransactionExtraHeatMintAuth`.

**Step 2B — Genesis-baked signer pubkey**

Add to `src/CryptoNoteConfig.h`:
```cpp
// Hardcoded CPI attestation signer (single key, Phase 2). 
// Replaced by federated multisig in Phase 3.
const char* const HEAT_CPI_SIGNER_PUBKEY = 
  "<32-byte hex pubkey to be filled in at activation>";

// Sanity bounds on attestation values (in CPI scale, index=100 at launch).
const uint64_t HEAT_CPI_MIN_INDEX = 80;          // 20% deflation floor
const uint64_t HEAT_CPI_MAX_INDEX = 1000;        // 10x runaway ceiling
const uint64_t HEAT_CPI_MAX_STEP_BPS = 100;      // max 1% change per attestation
const uint64_t HEAT_CPI_STALE_DAYS = 7;          // if no attestation for 7d, freeze CPI
```

**Step 2C — Block validation rule**

In `Blockchain::validateTransaction()` (or wherever extras are parsed), add a CPI attestation path:

```cpp
TransactionExtraCpiAttestation cpiAttest;
if (extractCpiAttestation(tx.extra, cpiAttest)) {
  // 1. Verify signature against hardcoded pubkey
  if (!verifyCpiSignature(cpiAttest)) return false;
  
  // 2. Sanity bounds
  if (cpiAttest.cpiIndexQ64 < cpiToQ64(parameters::HEAT_CPI_MIN_INDEX)) return false;
  if (cpiAttest.cpiIndexQ64 > cpiToQ64(parameters::HEAT_CPI_MAX_INDEX)) return false;
  
  // 3. Change rate cap (1% per attestation)
  uint64_t prev = m_piState.cpiCurrentValue.raw();
  uint64_t delta = std::abs(int64_t(cpiAttest.cpiIndexQ64) - int64_t(prev));
  if (delta > (prev * parameters::HEAT_CPI_MAX_STEP_BPS / 10000)) return false;
  
  // 4. Anti-replay: epochHeight must increase
  if (cpiAttest.epochHeight <= m_piState.cpiUpdateHeight) return false;
  
  // ... if all pass, defer state mutation to block apply
}
```

In `Blockchain::applyBlock()` (per-tx state mutations), after all validations pass, update `m_piState.cpiCurrentValue` and `m_piState.cpiUpdateHeight`. First valid attestation also snapshots `m_piState.cpiLaunchValue` and sets `m_piState.cpiOracleActive = true`.

**Step 2D — Attestation submission tool**

New CLI tool: `src/Tools/CpiSubmit/CpiSubmit.cpp`

```bash
$ cpi-submit --cpi-value 103.45 --signer-key <priv> --rpc http://localhost:18180
Built attestation: cpiIndexQ64=0x..., epochHeight=84203
Submitted tx: 0xabc...
```

The tool reads from a local `CpiOracleService` (Phase 1) or accepts a manual `--cpi-value` flag, signs, builds the tx, and broadcasts.

In production: a cron job on a trusted node runs `cpi-submit` every `HEAT_CPI_UPDATE_INTERVAL = 730` blocks (~4 days).

**Step 2E — Flip the stability mode**

In `src/CryptoNoteConfig.h`:
```cpp
// Change from:
const uint64_t HEAT_STABILITY_MODE = 2;
// To:
const uint64_t HEAT_STABILITY_MODE = 0;
```

This is a **hardfork-equivalent change**. Requires a hardfork height activation.

**Phase 2 exit criteria**:
- A submitted CPI attestation flips `cpiOracleActive` to `true` on all nodes.
- HEAT mint target ratio engages the CPI band branch.
- Submitting an attestation with bogus signature or out-of-bounds value gets rejected at validation.
- Submitting two attestations 2% apart gets the second rejected by the change-rate cap.

---

### Phase 3 — Manipulation defenses [2–3 days]

These build on Phase 2's basic sanity checks.

**Step 3A — Staleness gating in PI controller**

In `src/CryptoNoteCore/Blockchain.cpp` epoch handler:
```cpp
// If CPI hasn't been updated in HEAT_CPI_STALE_DAYS days, treat oracle as inactive.
uint64_t cpiAgeBlocks = currentHeight - m_piState.cpiUpdateHeight;
uint64_t cpiStaleBlocks = parameters::HEAT_CPI_STALE_DAYS * 24 * 60 * 60 / 
                          parameters::DIFFICULTY_TARGET;
if (cpiAgeBlocks > cpiStaleBlocks) {
  // Don't mutate cpiOracleActive (that's an explicit attestation operation);
  // instead, computeTargetRatio falls through to the bootstrap branch
  // because we pass a freshness flag.
}
```

Cleanest: add a `cpiIsFresh` parameter to `computeTargetRatio()` and have Mode 0 fall through when stale.

**Step 3B — Divergence alerting (off-chain only)**

In `CpiOracleService`, when sources diverge >10% from each other, log a warning. This doesn't change consensus — it's signal for human investigation.

**Step 3C — Multiplier hard clamps**

Already covered by `HEAT_CPI_MIN_INDEX = 80` and `HEAT_CPI_MAX_INDEX = 1000`. Verify these are enforced in the validation rule.

**Step 3D — Snapshot integrity**

Once `cpiLaunchValue` is snapshotted (first valid attestation), it must never change. Add an assertion in the attestation handler: if `cpiOracleActive` is already true, never overwrite `cpiLaunchValue`.

---

### Phase 4 — Federated CPI via threshold attestation [later, separate plan]

When Phase 2 stabilizes, replace the single-signer model with multisig threshold attestation:
- Multiple attestation nodes each run their own `CpiOracleService`.
- Each signs a partial attestation.
- An aggregator node combines ≥ N/2+1 signatures into a MuSig2 multisig attestation.
- Validators verify the threshold signature against the registered attestor set.

This phase is **out of scope here** — Phase 2 single-signer ships first.

---

## File-by-file change summary

### New files

| File | Purpose |
|---|---|
| `src/CryptoNoteCore/CpiOracle/CpiOracleService.{h,cpp}` | Median-of-sources CPI aggregator + thread management |
| `src/CryptoNoteCore/CpiOracle/TruflationFetcher.cpp` | Truflation API poller |
| `src/CryptoNoteCore/CpiOracle/PythBasketFetcher.cpp` | Pyth gold+silver+oil+EUR basket poller |
| `src/CryptoNoteCore/CpiOracle/BlsFetcher.cpp` | BLS CPI-U poller |
| `src/CryptoNoteCore/CpiOracle/FpiMarketFetcher.cpp` | FPI/USDC Uniswap TWAP reader (optional) |
| `src/Tools/CpiSubmit/CpiSubmit.{h,cpp}` | CLI to sign and submit attestation tx |
| `src/Tools/CpiSubmit/CMakeLists.txt` | Build target for the tool |
| `tests/CryptoNoteCore/CpiOracleTests.cpp` | Median + freshness + fallback tests |
| `tests/CryptoNoteCore/CpiAttestationTests.cpp` | Sig verification + bounds + rate-cap tests |

### Modified files

| File | Change |
|---|---|
| `src/CryptoNoteConfig.h` | Flip `HEAT_STABILITY_MODE = 2 → 0`; add `HEAT_CPI_SIGNER_PUBKEY`, `MIN_INDEX`, `MAX_INDEX`, `MAX_STEP_BPS`, `STALE_DAYS` |
| `src/CryptoNoteCore/TransactionExtra.h` | Add `TX_EXTRA_CPI_ATTESTATION = 0xC1` tag, `TransactionExtraCpiAttestation` struct, parse/serialize helpers |
| `src/CryptoNoteCore/TransactionExtra.cpp` | Parser implementation |
| `src/CryptoNoteCore/Blockchain.h` | Add `validateCpiAttestation()`, `applyCpiAttestation()` method declarations |
| `src/CryptoNoteCore/Blockchain.cpp` | Wire validation + apply paths; staleness gating in epoch handler |
| `src/CryptoNoteCore/Core.{h,cpp}` | Own a `CpiOracleService`, start/stop with daemon, expose `getCpiValue()` query |
| `src/Rpc/RpcServer.cpp` | New `/cpi` JSON endpoint |
| `src/CryptoNoteCore/PiController.cpp` | Optional: pass `cpiIsFresh` to `computeTargetRatio()` (or check internally via stored update height) |
| `src/CMakeLists.txt` | Add new CpiOracle/ sources to `CryptoNoteCore` lib; add `CpiSubmit` executable target |

### Files NOT modified (existing infrastructure stays)

- `src/CryptoNoteCore/PiController.h` — `PiControllerState` already correct
- Mode 0 logic in `PiController.cpp` — already correct
- `HeatMintEngine.{h,cpp}` — already consults `m_piState.redemptionPrice`, which is now driven by CPI
- Hardfork height table — only changes if we want activation gating (recommended)

---

## Hardfork considerations

Flipping `HEAT_STABILITY_MODE` and accepting a new tx extra type are consensus-breaking changes. Two options:

1. **Genesis bump** — set the mode to 0 from new chain genesis only. Not viable for a live chain.
2. **Hardfork height activation** (recommended):
   - Add a hardfork entry in `CryptoNoteConfig.h`'s `HARD_FORK_HEIGHTS` (or equivalent).
   - Before activation height: validation rejects `TX_EXTRA_CPI_ATTESTATION` tx, Mode is treated as 2 regardless of config.
   - At/after activation height: validation accepts attestations, Mode 0 engages once first attestation lands.
   - This gives node operators time to upgrade before consensus shifts.

The `cpiLaunchValue` snapshot lock-in (first valid attestation) gives us a clean migration point — there's no retroactive CPI compensation, holders are protected forward from the activation block.

---

## Verification

### Unit tests
- `CpiOracleTests`: median over {3, 5, 8} sources; freshness gating; empty-sources fallback; divergence detection
- `CpiAttestationTests`: valid attestation accepted; bad sig rejected; out-of-bounds rejected; replay (same epochHeight) rejected; rate-cap violation rejected
- `PiControllerTests` (extend existing): Mode 0 with `cpiOracleActive=true` produces expected ratio at CPI=100 vs CPI=130 vs CPI=80

### Integration tests
- Testnet daemon with `CpiOracleService` running → `/cpi` endpoint returns a value within 5 min
- `cpi-submit` to local daemon → tx mines → `m_piState.cpiCurrentValue` updates → `m_piState.cpiOracleActive` flips to true
- Mint HEAT at CPI=100 vs CPI=110: 10% fewer HEAT per XFG burned
- Burn HEAT during high-CPI period: returns 10% more XFG (purchasing power preserved both directions)

### Manual mainnet activation checklist
- [ ] Generate CPI signer keypair, secure private key in HSM/multisig vault
- [ ] Hardcode public key into `HEAT_CPI_SIGNER_PUBKEY` constant
- [ ] Choose activation height (give ≥ 30 days notice for operator upgrades)
- [ ] Update `HARD_FORK_HEIGHTS` table
- [ ] Release upgrade binary
- [ ] At activation block height: submit first attestation with `cpiIndexQ64 = launchIndex` (no-op CPI start)
- [ ] Verify `cpiOracleActive` flipped network-wide via block explorer
- [ ] Schedule cron for `cpi-submit` every `HEAT_CPI_UPDATE_INTERVAL` blocks
- [ ] Monitor `/cpi` endpoint health on all sentinel nodes
- [ ] Publish "HEAT is now a flatcoin" announcement after first ~30 days of stable operation

---

## Risks and mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| CPI signer key compromise | High | HSM custody initially; Phase 4 federation removes single point of failure |
| Truflation/Pyth/BLS all wrong simultaneously | Medium | Median-of-3 plus change-rate cap of 1%/attestation makes coordinated manipulation expensive |
| Sources go offline for >7 days | Low | Staleness gating drops back to pre-CPI behavior — peg degrades, doesn't fail |
| Off-chain → on-chain time skew | Low | `observationTime` field + height-based validation prevent stale attestations from being submitted late |
| Phase 1 fetcher infrastructure bugs flow into Phase 2 attestations | Medium | Phase 1 runs in production for ≥ 1 week before Phase 2 deployment, with `/cpi` endpoint monitored for anomalies |
| Hardfork coordination failure | Medium | Standard hardfork notification windows, signaling, and rollback procedures apply |
| HEAT holders confused by "1 HEAT ≠ $1 nominal" | UX | Phase-in with public docs + UI showing both nominal and real-USD value |

---

## Estimated effort

| Phase | Time |
|---|---|
| Phase 1 — CpiOracleService | 3–4 days |
| Phase 2 — On-chain attestation + Mode 0 flip | 4–5 days |
| Phase 3 — Manipulation defenses | 2–3 days |
| Tests + integration | 2–3 days |
| Manual mainnet activation + monitoring | 1–2 days (calendar time: 4+ weeks) |
| **Total engineering** | **~2–3 weeks** |
| **Total calendar to mainnet activation** | **~6–8 weeks** (includes hardfork notification windows) |

Phase 4 (threshold attestation federation) deferred to a separate ~2 week effort.

---

## Open design questions to settle before coding

1. **Hardfork activation height** — needs governance/operator consultation, not engineering. Defaults to "N + 30 days × blocks-per-day" after release.
2. **CPI signer key holder** — operations decision. Single internal key acceptable for Phase 2, federation by Phase 4.
3. **`HEAT_CPI_UPDATE_INTERVAL` cadence** — current value `730` blocks (~4 days) is reasonable. Could go shorter (`180` ≈ 1 day) for higher fidelity, longer (`2160` ≈ 12 days) to reduce signer load.
4. **Whether to surface CPI in user wallets** — recommend a separate "HEAT real value" UI field showing `HEAT_balance × cpi_multiplier` so holders see purchasing power growth. Out of scope for this plan but worth a follow-up.
5. **FPI market fetcher inclusion** — optional 4th source; adds complexity (on-chain ETH call dependency). Recommend Phase 1 omits this initially, add later if median diverges.

---

## Reuses from existing codebase

- `httplib::SSLClient` + `Json::Value` pattern from `SwapOfferRelay.cpp:476–509`
- `std::thread` + `std::atomic<bool> m_running` lifecycle pattern, same file
- `Crypto::Signature` + `check_signature()` pattern from `SwapOfferRelay.cpp:127`
- `TransactionExtra` parsing pattern, mirroring `TransactionExtraHeatMintAuth`
- `FixedPoint64` Q64.64 arithmetic from `Common/FixedPoint.h`
- `PiControllerState` serialization in `PiController.cpp:333–340`

No new dependencies, no new build targets beyond `CpiSubmit` and the new test files.

---

## Follow-up: companion docs to update

- `docs/design/HEAT_FLATCOIN_FORMULA.md` — already authored. Add a cross-reference to this plan.
- `docs/design/HEAT_STABILITY_FORMULA.md` — note that Mode 2 is being deprecated in favor of Mode 0 + CPI.
- `docs/HEAT_STABLECOIN_VISION.md` — update Model 4 (AMO) section to note CPI flatcoin path is now preferred.
- README — add HEAT flatcoin to the feature list once mainnet activation completes.

That's the full specification. The work is concrete, the infrastructure mostly exists, and the consensus model is settled by the signed-attestation pattern.
