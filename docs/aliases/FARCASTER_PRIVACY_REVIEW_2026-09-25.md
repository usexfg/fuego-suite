# Fuego alias and Farcaster privacy review

Date: 2026-09-25
Scope: current alias storage/RPC/transaction fields and the new Paradio Snapchain cast lookup. This is a source review, not a deployment or chain-history scan.

## Findings

### Public alias lookup exposes an address when one is recorded

`/get_alias`, `/get_alias_by_address`, and `/get_all_aliases` return `AliasEntry::ownerAddress`. The alias handler entries are available on the RPC server even in restricted mode; their handler-table flag controls whether they can run while the core is busy, and the handlers do not apply a restricted-mode check. `/get_all_aliases` returns every recorded alias and address, so the API is enumerable whenever the RPC endpoint is reachable. The API reference currently says this endpoint omits addresses and blocks mass surveillance; that statement does not match the implementation.

`WalletGreen::registerAlias` currently puts a randomly derived subaddress in the registration extra. A subaddress does not reveal its parent wallet by itself, but it is a public recipient identifier and can be tracked if reused for payouts. `SimpleWallet` attempts to omit `ownerAddress`, but `TransactionExtraAliasRegistration::isValid()` rejects an empty value, so that privacy path cannot currently produce a valid registration. Allowing empty values would also leave public alias resolution without an address unless a separate private or proof-based resolution design is added.

Alias registration extras are public chain data. Alias transfer extras explicitly serialize both `oldOwnerAddress` and `newOwnerAddress`, so transfers reveal those addresses and connect them to the alias. Removing addresses from an RPC response would not erase these historical transaction fields. Treat aliases as a public directory that can reveal recipient addresses, not as private Farcaster identity credentials.

`addressHash` is a persistent pseudonymous value derived from the spend and view public keys. It hides the raw keys when the preimage is unknown, but it still exposes equality across uses and is linkable when an observer already knows the candidate address. It is not a zero-knowledge proof.

Alias registration is not proof of wallet control. Consensus checks the registration data, network, alias uniqueness, and fee, then stores the supplied `ownerAddress` and `addressHash`; it does not prove that the address and hash correspond or that the registrant controls that wallet. Do not use alias registration alone to authorize a Paradio payout or to bind a Farcaster identity to a wallet.

### The Farcaster lookup does not link a wallet today

`GET /api/farcaster/casts/:fid/:hash` fetches a public cast and returns its FID, hash, text, and embed URLs. It accepts no Fuego address and has no Farcaster sign-in or wallet proof. The bridge currently binds to `127.0.0.1`; the route is read-only, but the FID/hash in the path can enter local access logs. The configured Snapchain provider also sees the FID/hash query and the Paradio server's network address. Running a trusted local Snapchain node avoids sending those lookups to a third-party indexer.

Farcaster casts and FIDs are public. A public post that names a Fire alias, or a public database/event that joins an FID to an alias, makes the social-to-chain relationship explicit. No such join is currently emitted by this lookup. During playback, the external media host will receive the listener's network request and can log its IP address and request time; the current bridge only returns embed URLs and does not fetch audio. The Snapchain provider sees requested FID/hash pairs and the Paradio server's network address.

## Rules for adding wallet linking

- Verify Farcaster sign-in on the server, and verify a separate Fuego wallet signature over a one-use, expiring, domain-separated challenge. Bind the challenge to the network, alias/address hash, purpose, and nonce. A supplied FID, username, alias, or address is not proof of control.
- Keep the FID-to-payout association private and access-controlled. Do not put an FID, username, cast hash, or wallet-link event in a public transaction extra or public alias metadata.
- Make public profile linking opt-in and explain that aliases and alias transfer history can expose recipient subaddresses. For rewards, use a private payout association and avoid reusing an externally listed alias address as the payout destination. Correct the alias API reference before presenting it as a privacy-preserving directory.
- Minimize and redact FID, cast hash, address, signature, and session values from application, proxy, and analytics logs. Do not persist raw authentication tokens or wallet signatures after verification unless there is a defined recovery need.
- Rotate or revoke an association when either account changes. A current wallet proof is required again before redirecting rewards.

The current Farcaster lookup is suitable as a content-reference adapter. It does not yet provide private identity linking, authenticated listening, or reward settlement.
