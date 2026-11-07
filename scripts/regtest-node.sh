#!/usr/bin/env bash
# Download a checksum-verified Bitcoin-family node, start it in -regtest and
# wait until its RPC answers. Used by the UTXO transport gate in CI
# (test_utxo_regtest, remediation plan 1.9).
#
#   scripts/regtest-node.sh <btc|ltc|bch> <workdir> <rpc_port>
#
# Every binary is checked against a published SHA-256 before it runs: Bitcoin
# Core against the checksum file served beside the release, Litecoin Core
# against the hash published with its v0.21.4 release, BCHN against the digest
# GitHub lists for its release asset. Signatures are not checked.
set -euo pipefail

chain="$1"
workdir="$2"
port="$3"
mkdir -p "$workdir"
cd "$workdir"

fetch() { curl -fsSL --retry 3 --retry-delay 5 -o "$2" "$1" 2>/dev/null; }

verify_listed() {  # <checksum file> <tarball>; the listed path may carry a directory
  expected="$(tr -d '\r' < "$1" |
              grep -ioE "^[0-9a-f]{64}[[:space:]]+\*?([^[:space:]]*/)?$2[[:space:]]*\$" | head -n 1 | cut -c1-64 |
              tr 'A-F' 'a-f' || true)"
  actual="$(sha256sum "$2" | cut -c1-64)"
  if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
    echo "$2: SHA-256 $actual is not the published ${expected:-<not listed in $1>}" >&2
    grep -n "$2" "$1" >&2 || true
    exit 1
  fi
  echo "$2: OK ($1)"
}

case "$chain" in
  btc)
    version=28.1
    tarball="bitcoin-$version-x86_64-linux-gnu.tar.gz"
    base="https://bitcoincore.org/bin/bitcoin-core-$version"
    fetch "$base/$tarball" "$tarball"
    fetch "$base/SHA256SUMS" SHA256SUMS
    verify_listed SHA256SUMS "$tarball"
    tar xzf "$tarball"
    daemon="$PWD/bitcoin-$version/bin/bitcoind"
    cli="$PWD/bitcoin-$version/bin/bitcoin-cli"
    ;;
  ltc)
    version=0.21.4
    tarball="litecoin-$version-x86_64-linux-gnu.tar.gz"
    fetch "https://download.litecoin.org/litecoin-$version/linux/$tarball" "$tarball"
    echo "857fc41091f2bae65c3bf0fd4d388fca915fc93a03f16dd2578ac3cc92898390  $tarball" | sha256sum -c -
    tar xzf "$tarball"
    daemon="$PWD/litecoin-$version/bin/litecoind"
    cli="$PWD/litecoin-$version/bin/litecoin-cli"
    ;;
  bch)
    # BCHN 28.x expired at the May 15, 2026 network upgrade and disables RPC.
    # 29.2.0 is not on download.bitcoincashnode.org and its GitHub release
    # carries no checksum file; the hash is the digest GitHub lists for the asset.
    version=29.2.0
    tarball="bitcoin-cash-node-$version-x86_64-linux-gnu.tar.gz"
    fetch "https://github.com/bitcoin-cash-node/bitcoin-cash-node/releases/download/v$version/$tarball" "$tarball"
    echo "23bbe9ff749136a9b832a2a59023ce76a5391322c1ea2f4f24b5b91f035081bc  $tarball" | sha256sum -c -
    mkdir -p bchn && tar xzf "$tarball" -C bchn
    daemon="$(find "$PWD/bchn" -path '*/bin/bitcoind' -type f | head -n 1)"
    cli="$(find "$PWD/bchn" -path '*/bin/bitcoin-cli' -type f | head -n 1)"
    ;;
  *)
    echo "unknown chain: $chain" >&2
    exit 2
    ;;
esac

datadir="$workdir/data"
mkdir -p "$datadir"
"$daemon" -regtest -daemon -datadir="$datadir" -rpcuser=gate -rpcpassword=gate \
  -rpcport="$port" -rpcbind=127.0.0.1 -rpcallowip=127.0.0.1 -fallbackfee=0.0002 \
  -server -listen=0 -txindex=0

for _ in $(seq 1 60); do
  if "$cli" -regtest -datadir="$datadir" -rpcuser=gate -rpcpassword=gate -rpcport="$port" \
       getblockcount >/dev/null 2>&1; then
    echo "$chain regtest node ready on 127.0.0.1:$port"
    exit 0
  fi
  sleep 1
done
echo "$chain regtest node did not answer on port $port" >&2
exit 1
