"""Recompute the sighash digests pinned in test_utxo_htlc.cpp.

Independent of the C++ signer: python-bitcoinlib's BIP143 implementation.
BCH's SIGHASH_ALL|FORKID digest is the BIP143 algorithm with hashtype 0x41.

    python3 -I utxo_htlc_sighash.py <script_hex> <amount> <hashtype> <raw_tx_hex>
"""
import sys

from bitcoin.core import CTransaction, b2x, x
from bitcoin.core.script import SIGVERSION_WITNESS_V0, CScript, SignatureHash


def main(script_hex, amount, hashtype, raw_tx_hex):
    tx = CTransaction.deserialize(x(raw_tx_hex))
    digest = SignatureHash(CScript(x(script_hex)), tx, 0, int(hashtype, 0),
                           amount=int(amount), sigversion=SIGVERSION_WITNESS_V0)
    print(b2x(digest))


if __name__ == "__main__":
    main(*sys.argv[1:])
