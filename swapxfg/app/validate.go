// swapxfg/app/validate.go
// Input validation helpers: per-chain address format checks and amount parsing.
package app

import (
	"encoding/hex"
	"fmt"
	"math/big"
	"regexp"
	"strconv"
	"strings"

	"golang.org/x/crypto/sha3"
)

// ── Address validation ─────────────────────────────────────────────────

var reETHAddr = regexp.MustCompile(`^0x[0-9a-fA-F]{40}$`)

// validateAddress checks that addr is a plausibly well-formed address for the
// named chain. Recognised chain names (case-insensitive): "eth", "sol", "bch",
// "xmr"/"xfg". Returns nil on success, a user-visible error otherwise.
func validateAddress(chain string, addr string) error {
	switch strings.ToLower(chain) {
	case "eth", "evm":
		return validateETHAddress(addr)
	case "sol":
		return validateSOLAddress(addr)
	case "bch":
		return validateBCHAddress(addr)
	case "xmr", "xfg":
		return validateXMRAddress(addr)
	default:
		// Unknown chain — accept but warn so callers can decide.
		return fmt.Errorf("validateAddress: unknown chain %q (skipping format check)", chain)
	}
}

// validateETHAddress enforces ^0x[0-9a-fA-F]{40}$.
// For mixed-case input it also applies EIP-55 checksum validation.
func validateETHAddress(addr string) error {
	if !reETHAddr.MatchString(addr) {
		return fmt.Errorf("invalid ETH address: must be 0x followed by 40 hex characters")
	}
	hexPart := addr[2:]
	hasUpper := strings.ContainsAny(hexPart, "ABCDEF")
	hasLower := strings.ContainsAny(hexPart, "abcdef")
	// Mixed-case → EIP-55 checksummed; validate it.
	if hasUpper && hasLower {
		if err := eip55ChecksumValid(addr); err != nil {
			return err
		}
	}
	return nil
}

// eip55ChecksumValid verifies the EIP-55 mixed-case checksum for an ETH
// address using an inline Keccak-256 implementation so we avoid a heavy
// dependency for a simple check.
func eip55ChecksumValid(addr string) error {
	lower := strings.ToLower(addr[2:])
	hash := keccak256Hex([]byte(lower))
	for i, c := range addr[2:] {
		hashNibble := hash[i]
		if hashNibble >= '8' {
			// This nibble should be upper-case.
			if c >= 'a' && c <= 'f' {
				return fmt.Errorf("invalid EIP-55 checksum for ETH address %s", addr)
			}
		} else {
			// This nibble should be lower-case.
			if c >= 'A' && c <= 'F' {
				return fmt.Errorf("invalid EIP-55 checksum for ETH address %s", addr)
			}
		}
	}
	return nil
}

// validateSOLAddress base58-decodes addr and checks the decoded length is
// exactly 32 bytes (a standard Solana public key / program ID).
func validateSOLAddress(addr string) error {
	if addr == "" {
		return fmt.Errorf("invalid SOL address: empty")
	}
	decoded, err := base58Decode(addr)
	if err != nil {
		return fmt.Errorf("invalid SOL address: not valid base58: %w", err)
	}
	if len(decoded) != 32 {
		return fmt.Errorf("invalid SOL address: decoded length is %d, want 32", len(decoded))
	}
	return nil
}

// validateBCHAddress accepts:
//   - CashAddr format: "bitcoincash:q..." or bare "q..."/"p..."
//   - Legacy P2PKH/P2SH: starts with '1' or '3', length 25-34
func validateBCHAddress(addr string) error {
	if addr == "" {
		return fmt.Errorf("invalid BCH address: empty")
	}
	// CashAddr with prefix
	if strings.HasPrefix(addr, "bitcoincash:") {
		rest := addr[len("bitcoincash:"):]
		if len(rest) < 40 {
			return fmt.Errorf("invalid BCH address: cashaddr payload too short")
		}
		if !strings.HasPrefix(rest, "q") && !strings.HasPrefix(rest, "p") {
			return fmt.Errorf("invalid BCH address: cashaddr must start with 'q' or 'p' after prefix")
		}
		return nil
	}
	// CashAddr without prefix (bare form)
	if strings.HasPrefix(addr, "q") || strings.HasPrefix(addr, "p") {
		if len(addr) < 40 {
			return fmt.Errorf("invalid BCH address: cashaddr payload too short")
		}
		return nil
	}
	// Legacy base58check
	if strings.HasPrefix(addr, "1") || strings.HasPrefix(addr, "3") {
		if len(addr) < 25 || len(addr) > 34 {
			return fmt.Errorf("invalid BCH address: legacy address length %d out of range [25,34]", len(addr))
		}
		return nil
	}
	return fmt.Errorf("invalid BCH address: unrecognised format (expected bitcoincash:q..., q..., 1..., or 3...)")
}

// validateXMRAddress validates XMR/XFG mainnet addresses:
//   - Standard address: starts with '4', length 95
//   - Subaddress: starts with '8', length 95
//   - Validates the 4-byte keccak-256 checksum (Monero variant of base58check)
func validateXMRAddress(addr string) error {
	if addr == "" {
		return fmt.Errorf("invalid XMR address: empty")
	}
	if len(addr) != 95 {
		return fmt.Errorf("invalid XMR address: length is %d, want 95", len(addr))
	}
	if !strings.HasPrefix(addr, "4") && !strings.HasPrefix(addr, "8") {
		return fmt.Errorf("invalid XMR address: must start with '4' (standard) or '8' (subaddress)")
	}

	decoded, err := base58Decode(addr)
	if err != nil {
		return fmt.Errorf("invalid XMR address: not valid base58: %w", err)
	}
	if len(decoded) != 65 {
		return fmt.Errorf("invalid XMR address: decoded length is %d, want 65 (64 bytes + 4 checksum)", len(decoded))
	}

	payload := decoded[:64]
	checksumProvided := decoded[64:]
	checksumComputed := keccak256Checksum(payload)

	if !bytesEqual(checksumProvided, checksumComputed) {
		return fmt.Errorf("invalid XMR address: checksum mismatch")
	}
	return nil
}

func keccak256Checksum(payload []byte) []byte {
	h := sha3.NewLegacyKeccak256()
	h.Write(payload)
	hash := h.Sum(nil)
	return hash[:4]
}

func bytesEqual(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// ── Amount parsing ─────────────────────────────────────────────────────

// parseAmount parses a user-supplied decimal amount string and validates it is
// positive and within a sane range (0, 1e15]. Returns the float64 value.
func parseAmount(s string) (float64, error) {
	amt, err := strconv.ParseFloat(strings.TrimSpace(s), 64)
	if err != nil {
		return 0, fmt.Errorf("invalid amount %q: %w", s, err)
	}
	if amt <= 0 {
		return 0, fmt.Errorf("amount must be positive, got %g", amt)
	}
	if amt > 1e15 {
		return 0, fmt.Errorf("amount out of range: %g (max 1e15)", amt)
	}
	return amt, nil
}

// parseAmountAtomic parses a user-supplied amount and converts it to atomic
// units by multiplying by the given decimals factor (e.g. 1e7 for XFG,
// 1e8 for BTC/BCH, 1e9 for SOL lamports, 1e18 for ETH wei).
func parseAmountAtomic(s string, decimals float64) (uint64, error) {
	amt, err := parseAmount(s)
	if err != nil {
		return 0, err
	}
	atomic := uint64(amt * decimals)
	if atomic == 0 {
		return 0, fmt.Errorf("amount %g is too small (rounds to zero at %g decimals)", amt, decimals)
	}
	return atomic, nil
}

// ── Inline Keccak-256 ─────────────────────────────────────────────────

// keccak256Hex returns the lower-case hex-encoded Keccak-256 hash of data.
// Used for EIP-55 address checksum validation.
func keccak256Hex(data []byte) string {
	h := sha3.NewLegacyKeccak256()
	h.Write(data)
	return hex.EncodeToString(h.Sum(nil))
}

// ── Inline base58 decoder ──────────────────────────────────────────────

const base58Alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

// base58Decode decodes a Bitcoin-style base58 string to bytes.
func base58Decode(s string) ([]byte, error) {
	bigInt := big.NewInt(0)
	base := big.NewInt(58)

	for _, c := range s {
		idx := strings.IndexRune(base58Alphabet, c)
		if idx < 0 {
			return nil, fmt.Errorf("invalid base58 character %q", c)
		}
		bigInt.Mul(bigInt, base)
		bigInt.Add(bigInt, big.NewInt(int64(idx)))
	}

	decoded := bigInt.Bytes()

	// Count leading '1' characters (they encode leading zero bytes).
	nLeadingZeros := 0
	for _, c := range s {
		if c == '1' {
			nLeadingZeros++
		} else {
			break
		}
	}

	result := make([]byte, nLeadingZeros+len(decoded))
	copy(result[nLeadingZeros:], decoded)
	return result, nil
}
