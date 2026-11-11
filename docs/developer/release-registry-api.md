# Release Registry API

Read-only proof that a song's bytes were registered, by whom, and at which height.

The registry lives on the Fuego daemon. There is no separate service and no SDK. A platform calls three JSON routes and folds a merkle path itself.

**Status:** the index is empty in every shipped build. Nothing writes to it yet — see [What is absent](#what-is-absent). Every call currently returns `found: false`. Do not treat a `found: false` as "this file is unregistered on the network"; it means "this daemon has no such leaf."

## What a caller sends

`content_hash` is the hex-encoded SHA-256 of the audio file, 64 lowercase characters.

The daemon never fetches, decodes, or hashes audio. You compute the hash from the file you hold and send it. That is the whole trust boundary: the daemon attests to what was registered against that hash, not to what the bytes sound like.

## Routes

All three accept `POST` with a JSON body. `GET` also works for requests with no required fields, because the handler reads the body and an empty body parses to defaults — but do not rely on that; send `POST`.

### `POST /get_release_root`

No fields. Returns the current root over every registered release.

```json
{
  "merkle_root": "0000000000000000000000000000000000000000000000000000000000000000",
  "count": 0,
  "status": "OK"
}
```

An empty index returns the zero hash and `count: 0`. That is the correct answer for a fresh daemon, not an error.

### `POST /get_release`

```json
{ "content_hash": "<64 hex chars>" }
```

```json
{
  "found": true,
  "content_hash": "<echo of the request>",
  "signer": "<64 hex chars, Ed25519 public key>",
  "height": 12345,
  "metadata_hex": "<opaque hex, up to 512 chars>",
  "status": "OK"
}
```

`metadata_hex` is opaque registration metadata. The daemon does not interpret it; do not assume it is JSON or UTF-8.

A malformed hash — wrong length, non-hex — is a miss, not an error. You get `found: false` and `status: "OK"`. The daemon does not distinguish "you typed nonsense" from "not registered" in the response.

### `POST /get_release_proof`

```json
{ "content_hash": "<64 hex chars>" }
```

```json
{
  "found": true,
  "content_hash": "<echo of the request>",
  "leaf_hash": "<64 hex chars>",
  "merkle_root": "<64 hex chars>",
  "height": 12345,
  "leaf_index": 7,
  "proof_path": ["<sibling hex>", "..."],
  "proof_indices": [0, 1, 0],
  "status": "OK"
}
```

`leaf_hash` is **not** your content hash. The leaf binds four things:

```text
preimage = content_hash[32] || signer[32] || height_le64[8] || metadata_len_le16[2] || metadata
leaf     = cn_fast_hash(preimage)
```

So the same file registered by two different signers produces two different leaves, and a proof for one does not vouch for the other. A later metadata edit changes the leaf too.

## Verifying a proof without a node

The fold below only proves that the path is *internally consistent* with the `merkle_root` in the same response. A daemon that lies can return a fabricated leaf, a fabricated path, and a fabricated root that agree perfectly. **Comparing `current` to `merkle_root` from the same response verifies arithmetic, not authenticity.**

### Ordered-by-contract invariant

The registry is **ordered-by-contract**, not enforced-by-code: insertion order determines root determinism. If two nodes are fed the same records in different orders, the merkle roots diverge and proofs from one node will not verify against the other. The feed must be ordered; `insert()` does not enforce height monotonicity. See `CHANGELOG.agent.md` W3.

The root must come from somewhere you already trust. Today there is nowhere: the registry root is not committed into a block header and is not exposed by any independent channel, so a proof fetched over HTTP from a single daemon cannot be trusted at all. This becomes meaningful only when the root is pinned — from a block header field, a checkpoint, or an agreement across independent nodes. Until then, treat `/get_release_proof` as a shape demonstration.

Given a root you obtained independently:

`cn_fast_hash` is Keccak-256, **not** SHA-256, and **not** Solidity's `keccak256` applied to a differently-framed buffer. If you reimplement the fold in another language, use plain Keccak-256 over the 64-byte concatenation.

```text
current = leaf_hash
for i in 0..len(proof_path)-1:
    sibling = proof_path[i]
    if proof_indices[i] == 0:
        current = cn_fast_hash(current || sibling)   # you are the left child
    else:
        current = cn_fast_hash(sibling || current)   # you are the right child
assert current == pinned_root          # NOT res.merkle_root
```

`proof_indices[i]` is bit `i` of `leaf_index`, which is why you can also derive it yourself as `(leaf_index >> i) & 1`. Both are returned so you can check the daemon did not lie about one of them — but check them against each other, and check `len(proof_path)` is consistent with `leaf_index`.

When a level has an odd number of nodes, the trailing node is paired with itself. That means the last sibling in a path can equal `current`. This is normal; do not treat it as a malformed proof.

A single-leaf registry returns an empty `proof_path` and `leaf_hash == merkle_root`.

## What is absent

- **No registration route.** There is no `POST /register_release`. A route that writes the index from an HTTP body is a website, not a chain. Registrations are meant to arrive as transaction extras and be indexed at block connect.
- **No transaction-extra parser.** `TX_EXTRA_DIGM_ALBUM` (`0x0A`) is a `#define` only. It is not in the `TransactionExtraField` variant and nothing parses it.
- **No block connect feed.** The index is fed by unit tests. It does not reflect chain state.
- **No reorg undo.** `ReleaseRegistry::eraseByHeight` exists and is tested, but nothing calls it on block disconnect.
- **No signature check.** The leaf commits to a `signer` key, but no code verifies that the signer actually signed anything. A leaf proves "this key was recorded against these bytes at this height", not "this key authorized it".

Until the feed lands, treat every response as a demonstration of the API shape.

## Why there is no separate `xfgapi`

There is no `xfgapi` in this tree. Open JSON routes are registered in one table, `RpcServer::s_handlers` in `src/Rpc/RpcServer.cpp`, next to `/heat_metrics` and `/get_commitment_merkle_proof`. Adding a second process would mean a second merkle index, which means a second root, which makes the proof meaningless.

If a facade is ever built, it proxies these three routes. It does not own an index.

## Related

- Implementation: `src/CryptoNoteCore/ReleaseRegistry.h`
- Plan and known gaps: `docs/superpowers/plans/2026-10-28-release-registry-api.md`
