#!/usr/bin/env bash
# Download a checksum-verified Bitcoin-family node, start it in -regtest and
# wait until its RPC answers. Used by the UTXO transport gate in CI
# (test_utxo_regtest, remediation plan 1.9).
#
#   scripts/regtest-node.sh <btc|ltc|bch> <workdir> <rpc_port>
#
# Every binary is checked against a published SHA-256 before it runs: Bitcoin
# Core and BCHN against the checksum file served beside the release, Litecoin
# Core against the hash published with its v0.21.4 release. Signatures on the
# checksum files are not checked.
set -euo pipefail

chain="$1"
workdir="$2"
port="$3"
mkdir -p "$workdir"
cd "$workdir"

fetch() { curl -fsSL --retry 3 --retry-delay 5 -o "$2" "$1" 2>/dev/null; }

verify_listed() {  # <checksum file> <tarball>; the listed path may carry a directory
  expected="$(tr -d '\r' < "$1" |
              grep -ioE "^[0-9a-f]{64}[[:space:]]+\*?([^[:space:]]*/)?$2\$" | head -n 1 | cut -c1-64 |
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
    version=28.0.1
    # BCHN publishes binaries only on its download host: the tarball under
    # linux/, and one "SHA256SUMS.<version>.asc.<signer>" file per signer.
    base="https://download.bitcoincashnode.org/releases/$version"
    tarball="$(curl -fsSL --retry 3 "$base/linux/" 2>/dev/null |
               grep -oE 'href="[^"/]*x86_64[^"/]*linux[^"/]*\.tar\.gz"' | grep -v -i debug |
               sed -e 's/^href="//' -e 's/"$//' | head -n 1 || true)"
    sums_files="$(curl -fsSL --retry 3 "$base/" 2>/dev/null |
                  grep -oE "SHA256SUMS\.$version\.asc\.[A-Za-z0-9_.-]+" | sort -u || true)"
    if [ -z "$tarball" ] || [ -z "$sums_files" ] || ! fetch "$base/linux/$tarball" "$tarball"; then
      echo "BCHN $version: tarball or checksum files not found. Listings:" >&2
      for dir in "$base/" "$base/linux/"; do
        echo "$dir" >&2
        curl -sSL "$dir" 2>&1 | grep -oE 'href="[^"]+"' | head -n 40 >&2 || true
      done
      exit 1
    fi
    # Every signer's checksum file must list the same hash for the tarball.
    for sums in $sums_files; do
      fetch "$base/$sums" "$sums" || { echo "could not download $base/$sums" >&2; exit 1; }
      verify_listed "$sums" "$tarball"
    done
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
