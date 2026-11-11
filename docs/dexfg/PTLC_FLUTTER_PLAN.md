# PTLC Flutter Wallet Plan — fuego-flutter-wallet DeXFG

> Target: `/Users/aejt/DEXFG/fuego-flutter-wallet`  (Flutter 3.x + `rust-fuego-wallet`)
> Companion: `PTLC_PLAN.md` (daemon), `PTLC_USER_WALKTHROUGH.md` (user), `PTLC_DEV_GUIDE.md:4` (daemon per-chain)
> Scope: Expose PTLC negotiation, badge, and `requirePtlc` toggle in the GUI without breaking old daemons.

---

## 1. Where we are

| Layer | File | Current | PTLC delta |
|-------|------|---------|------------|
| **Dart models** | `lib/models/swap_models.dart:6` `SwapPairSdk` 12 values, `SwapInfo` `pair/xfgAmount/state` only | Add `SwapLockType` enum + `lockType/ptlcPoint/requirePtlc` fields to `SwapInfo`/`SwapStatusSdk` |
| **Chain metadata** | `lib/models/chain_info.dart:6` `ChainInfo.info` `htlc` string per chain | Add `ptlc` field per chain (`P2WSH point / HashedTimelock+event / adaptor`) and `supportsPtlc` bool |
| **Daemon client** | `lib/services/swap_daemon_client.dart:60` `initiateSwap({pair,…})` 11 params | Add `requirePtlc/lockType/ptlcPoint` passthrough to `initiate_swap` RPC |
| **DEX UI** | `lib/screens/dex/dex_screen.dart:1` 4 tabs `Orderbook/Accept/Direct/History` | Add badge + toggle + detail sheet |
| **Rust proxy** | `rust-fuego-wallet/src/swap.rs` proxy to `xfg-swapd` 18902 | Pass through new JSON fields `lock_type/require_ptlc/ptlc_point` |
| **Daemon** | `src/SwapDaemon/SwapTypes.h:77` 28 pairs | Already PTLC dual-stack — flutter just surfaces it |

Flutter is a **proxy**: real state machine lives in `xfg-swapd`. Flutter must not reimplement adaptor crypto; it just displays what the daemon negotiated and forwards the user's `requirePtlc` choice.

---

## 2. Phase breakdown (5 days total, 75% confidence +20% buffer → 6 days)

### Phase F0 — Models + wire (1 day)
**Files:** `lib/models/swap_models.dart`, `rust-fuego-wallet/fuego-sdk/types.rs` (if mirrored)

```dart
// swap_models.dart:6 after SwapPairSdk
enum SwapLockTypeSdk {
  htlc(0, 'HTLC'),
  ptlc(1, 'PTLC'),
  bridge(2, 'BRIDGE');
  final int id; final String label; const SwapLockTypeSdk(this.id, this.label);
  static SwapLockTypeSdk fromId(int id) => values.firstWhere((v)=>v.id==id, orElse:()=> htlc);
  static SwapLockTypeSdk fromString(String s){
    final u=s.toUpperCase();
    if(u=='PTLC') return ptlc;
    if(u=='BRIDGE'||u=='PTLC_HTLC_BRIDGE') return bridge;
    return htlc;
  }
}
```

Extend `SwapInfo` `SwapStatusSdk`:
```dart
final SwapLockTypeSdk lockType; // default htlc for old daemon JSON
final String ptlcPoint;         // hex 64, empty if HTLC
final bool requirePtlc;
factory ... {
  lockType: SwapLockTypeSdk.fromId(j['lockType'] as int? ?? j['lock_type'] as int? ?? 0)
           ?? SwapLockTypeSdk.fromString(j['lockTypeName']?.toString()??''),
  ptlcPoint: j['ptlcPoint']?.toString() ?? j['ptlc_point']?.toString() ?? '',
  requirePtlc: j['requirePtlc'] as bool? ?? j['require_ptlc'] as bool? ?? false,
}
```

**Rust:** if `fuego-sdk` mirrors `SwapInfo`, add same fields with `#[serde(default)]` so old daemon JSON (no `lockType`) deserializes to `HTLC`.

**Acceptance:** `dart analyze` 0 errors, `cargo check -p fuego-sdk` ok, unit test `swap_models_test.dart` roundtrips `{"lockType":1}` → `ptlc`.

### Phase F1 — ChainInfo + daemon client (1 day)
**Files:** `lib/models/chain_info.dart:51`, `lib/services/swap_daemon_client.dart:60`

`chain_info.dart` add per-chain `ptlc` descriptor and `supportsPtlc` set:

```dart
static const Map<String, String> ptlc = {
  'BTC': 'P2WSH point commitment (Taproot scriptless Phase2)',
  'LTC': 'P2WSH point commitment',
  'SOL': 'ed25519 adaptor ClaimPtlc (Phase4) — now bridge',
  'ETH': 'HashedTimelock + PtlcLocked event (bridge)',
  'XMR': 'native adaptor (no HTLC)',
};
static const Set<String> supportsPtlc = {'BTC','LTC','XMR','ZANO'}; // Phase1 bridge still false for EVM/SOL
```

Or derive `supportsPtlc` from `ChainTypeSdk.isPtlcCapable` getter.

`swap_daemon_client.dart` extend `initiateSwap`:
```dart
Future<String> initiateSwap({
  ...
  bool requirePtlc = false,
  String? ptlcPoint, // usually daemon generates; flutter only forwards if user pasted
}) async {
  final params = <String,dynamic>{..., 'require_ptlc': requirePtlc, 'lock_type': requirePtlc?1:0};
  // daemon will negotiate BRIDGE vs PTLC; flutter does not set lock_type directly except for require
}
```

Update `SwapInfo.fromJson` to surface `ptlcPoint` from `j['params']['ptlcPoint']`.

**Rust proxy:** ensure `src/swap.rs` `initiate_swap` forwards `require_ptlc`/`ptlc_point` verbatim to `xfg-swapd` JSON.

### Phase F2 — DexCubit + services (1 day)
**Files:** `lib/bloc/dex/dex_cubit.dart`, `lib/services/daemon_manager.dart:1`

`DexState` add `bool requirePtlc` + `SwapLockTypeSdk lastLockType` for badge.

`DexCubit`:
- `toggleRequirePtlc(bool v)` → `emit(state.copyWith(requirePtlc:v))` + persist via `swap_config_service.dart:1` `SharedPreferences`.
- `initiateSwap()` reads `state.requirePtlc` and passes to `SwapDaemonClient.initiateSwap`.
- `pollSwaps()` maps `SwapInfo.lockType` to badge color: `ptlc` green, `bridge` amber, `htlc` grey.
- Downgrade error handling: if RPC throws `requirePtlc abort` → `emit(error: 'PTLC required but $pair does not support PTLC — turn off the toggle or pick XMR/BTC')`.

`daemon_manager.dart` log PTLC negotiation: `PTLC negotiation: $pair → ${info.lockType}`.

### Phase F3 — UI (1.5 days)
**Files:** `lib/screens/dex/dex_screen.dart:1`, `lib/widgets/*`, `lib/screens/dex/peer_swap_screen.dart`

1. **Pair bar badge** `_buildPairBar`: after `ChainInfo.colors[ ticker ]` add:
```dart
Container(
  padding: EdgeInsets.symmetric(horizontal:6, vertical:2),
  decoration: BoxDecoration(color: lockTypeColor(state.lastLockType), borderRadius: BorderRadius.circular(4)),
  child: Text(state.lastLockType.label, style: TextStyle(fontSize:10, fontWeight: FontWeight.w700, color: Colors.white)),
)
```
`lockTypeColor`: `ptlc` `0xFF2E7D32`, `bridge` `0xFFEF6C00`, `htlc` `0xFF6B7280`.

2. **Trade form toggle** `_buildTradeForm`: add `SwitchListTile` below amount/rate:
```dart
SwitchListTile(
  title: Text('Require PTLC (no HTLC fallback)', style: TextStyle(fontSize:12)),
  subtitle: Text(pairSupportsPtlc ? 'Enforces per-hop decorrelation' : 'This chain is HTLC-only — will abort if on', style: TextStyle(fontSize:10, color: Colors.grey)),
  value: state.requirePtlc,
  onChanged: pair == 'XMR' ? null : (v)=> context.read<DexCubit>().toggleRequirePtlc(v),
)
```
Disable toggle for XMR (always PTLC).

3. **Swap detail sheet** tap `History` or `SwapInfo` tile → bottom sheet showing `T`, `H(t)`, `Q`, `DLEQ ok`, `ptlcPoint`, `chainState` truncated + `Copy` buttons. Reuse `bitcoin_reserve_proof.dart:1` proof UI pattern.

4. **Chain selector** `_showChainSelector`: add small `PTLC` dot/green border for `supportsPtlc` chains.

**a11y:** badge has `Semantics(label: 'Lock type $label')`.

### Phase F4 — Tests + polish (0.5 day + buffer)
- `flutter analyze lib/models/swap_models.dart lib/services/swap_daemon_client.dart lib/screens/dex/dex_screen.dart` → 0 errors.
- **Widget test** `test/swap_locktype_test.dart`: `SwapInfo.fromJson({'lockType':1})` → `ptlc`; badge color; `toggleRequirePtlc` persists.
- **Integration** with live `xfg-swapd` (regtest): initiate `XFG/BTC` with `requirePtlc false` → `BRIDGE`, with `true` → `PTLC` after Phase2; `XFG/ETH` with `true` → abort error shown.
- **Docs** link `PTLC_USER_WALKTHROUGH.md:1` in DexScreen help icon `?` → `url_launcher` to `docs/PTLC_USER_WALKTHROUGH.md`.

---

## 3. Failure modes already handled (do not re-add)

- Old daemon (no `lockType`): Dart defaults to `htlc`, UI shows grey badge, toggle still works but daemon ignores `require_ptlc` — safe.
- New flutter + old daemon `chainState` with `|ptlc:` suffix: daemon strips `|` `BtcChainClient.cpp:132`, flutter just displays `ptlcPoint` if present.
- Downgrade: flutter never sets `lockType` directly; daemon decides via `SwapPtlcLock::negotiateLockType` + `localRequire || peerRequire` `SwapDaemon.cpp:3505`.

---

## 4. What NOT to do

- Do not add adaptor crypto in Dart. All `s' = k+e*sk+t` lives in `src/crypto/secp_adaptor.cpp:70` and `fuego-swapd-adaptor`. Flutter only displays hex.
- Do not renumber `SwapPairSdk` ids — persisted swaps depend on `SwapTypes.h:77` uint8_t.
- Do not block `initiate` if `requirePtlc` fails at model layer; let daemon return the abort error so the user sees the exact log line.

---

## 5. Ship checklist

- [ ] `flutter analyze` 0 errors
- [ ] `dart format lib/models/swap_models.dart lib/services/swap_daemon_client.dart`
- [ ] `cargo check -p fuego-sdk` if types mirrored
- [ ] Manual: `XFG/ETH` bridge badge amber, `XFG/XMR` PTLC green
- [ ] Manual: `Require PTLC on + ETH` shows abort error, `off` succeeds
- [ ] Screenshot DeXFG pair bar + trade form toggle for PR

Estimate: 5 days build + 1 buffer = 6 days, 75% confidence.

