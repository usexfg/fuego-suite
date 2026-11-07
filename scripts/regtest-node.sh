#!/usr/bin/env bash
# Download a checksum-verified Bitcoin-family node, start it in -regtest and
# wait until its RPC answers. Used by the UTXO transport gate in CI
# (test_utxo_regtest, remediation plan 1.9).
#
#   scripts/regtest-node.sh <btc|ltc|bch> <workdir> <rpc_port>
#
# Every binary is checked against a published SHA-256 before it runs: Bitcoin
# Core and BCHN against the checksum file served beside the release, Litecoin
# Core against the hash published with its v0.21.4 release.
set -euo pipefail

chain="$1"
workdir="$2"
port="$3"
mkdir -p "$workdir"
cd "$workdir"

fetch() { curl -fsSL --retry 3 --retry-delay 5 -o "$2" "$1"; }

verify_listed() {  # <checksum file> <tarball>
  grep -E "[[:space:]]\*?$2\$" "$1" | head -n 1 | sed 's/\*//' | sha256sum -c -
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
    version=28.0.1
    tarball="bitcoin-cash-node-$version-x86_64-linux-gnu.tar.gz"
    base="https://download.bitcoincashnode.org/releases/$version"
    fetch "$base/linux/$tarball" "$tarball"
    fetch "$base/bitcoin-cash-node-sha256sums.$version.asc" SHA256SUMS
    verify_listed SHA256SUMS "$tarball"
    tar xzf "$tarball"
    daemon="$PWD/bitcoin-cash-node-$version/bin/bitcoind"
    cli="$PWD/bitcoin-cash-node-$version/bin/bitcoin-cli"
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
