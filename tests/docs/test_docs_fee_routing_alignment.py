#!/usr/bin/env python3
"""Assert public Mintlify docs match live CryptoNoteConfig.h + deposit architecture.

Drives real config header and every page linked from docs/docs.json.
"""
from __future__ import annotations

import json
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "src" / "CryptoNoteConfig.h"
DOCS = ROOT / "docs"
DOCS_JSON = DOCS / "docs.json"

# Residual product myths that must not appear as current fact on nav pages.
# Each entry: (compiled regex, short name). Matches are FAIL unless context
# clearly marks them as wrong/deprecated/historical.
RESIDUAL_FACT_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (
        re.compile(
            r"Your\s+XFG\s+is\s+inaccessible\s+until\s+the\s+maturity",
            re.I,
        ),
        "XFG-as-CD lockup",
    ),
    (
        re.compile(
            r"\block(?:ing)?\s+XFG\s+(?:for|as)\s+(?:a\s+)?(?:fixed\s+)?(?:term|CD|deposit)\b",
            re.I,
        ),
        "lock XFG for CD",
    ),
    (
        re.compile(
            r"\b0\.3%\s+(?:fee\s+)?income\b|\b0\.3%\s+fee\s+(?:per\s+trade|income)\b|\b0\.3%\s+per\s+(?:pool\s+)?trade\b",
            re.I,
        ),
        "0.3% fee as Hearth/LP product fact",
    ),
    (
        re.compile(
            r"\b80%\s+of\s+(?:epoch\s+)?(?:atomic\s+)?swap\s+fees\b|\b80%\s+CD\b",
            re.I,
        ),
        "80% swap fee split",
    ),
    (
        re.compile(
            r"Generate\s+STARK\s+proofs?\s+for\s+HEAT\s+burns",
            re.I,
        ),
        "STARK proofs for HEAT burns (current use)",
    ),
    (
        re.compile(
            r"HEAT\s+token\s+value\s+depends\s+on\s+COLDAO|claim\s+HEAT\s+via\s+off-chain|off-chain\s+claiming",
            re.I,
        ),
        "COLDAO/off-chain HEAT claim as live",
    ),
    (
        re.compile(
            r'deposit_type"\s*:\s*"COLD"|list_cold\b',
            re.I,
        ),
        "COLD deposit CLI/API as current",
    ),
]

DENIAL_MARKERS = re.compile(
    r"\b("
    r"not|never|wrong|incorrect|outdated|deprecated|ignore|myth|"
    r"cannot|do\s+not|don't|removed|no\s+longer|superseded|"
    r"historical|must\s+not|false|inaccurate|legacy"
    r")\b",
    re.I,
)


def parse_uint_const(name: str, text: str) -> int:
    pat = rf"\b{re.escape(name)}\s*=\s*([0-9]+)\s*;"
    m = re.search(pat, text)
    if not m:
        raise AssertionError(f"constant {name} not found in CryptoNoteConfig.h")
    return int(m.group(1))


def nav_groups(cfg: dict) -> list[dict]:
    nav = cfg.get("navigation", [])
    if isinstance(nav, dict):
        return list(nav.get("groups") or [])
    return list(nav)


def nav_page_paths() -> list[Path]:
    cfg = json.loads(DOCS_JSON.read_text())
    paths: list[Path] = []
    for group in nav_groups(cfg):
        for p in group.get("pages", []):
            for ext in (".mdx", ".md"):
                f = DOCS / f"{p}{ext}"
                if f.exists():
                    paths.append(f)
                    break
            else:
                raise AssertionError(f"nav page missing: {p}")
    return paths


def residual_hits(text: str) -> list[tuple[str, str]]:
    """Return (pattern_name, context) for residual myths stated as fact."""
    hits: list[tuple[str, str]] = []
    for pat, name in RESIDUAL_FACT_PATTERNS:
        for m in pat.finditer(text):
            start = max(0, m.start() - 120)
            end = min(len(text), m.end() + 120)
            ctx = text[start:end].replace("\n", " ")
            # Deny if the match sits in a clear "this is wrong" block
            if DENIAL_MARKERS.search(ctx):
                # Still fail if the sentence affirms current product without
                # negation on the same clause (e.g. "Your XFG is inaccessible")
                # Context window can include "cannot withdraw" nearby — require
                # denial within ±40 chars of match for skip.
                near = text[max(0, m.start() - 40) : min(len(text), m.end() + 40)]
                if DENIAL_MARKERS.search(near):
                    continue
                # Explicit "incorrect"/"superseded"/"must not" lists nearby
                if re.search(
                    r"(incorrect|superseded|must not|do not treat|wrong model|not the primary|deprecated)",
                    ctx,
                    re.I,
                ):
                    continue
            hits.append((name, ctx.strip()))
    return hits


class DocsFeeAlignment(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cfg_text = CONFIG.read_text()
        cls.swap_bps = parse_uint_const("SWAP_FEE_RATE_BPS", cls.cfg_text)
        cls.cd = parse_uint_const("SWAP_FEE_CD_SHARE_PCT", cls.cfg_text)
        cls.bonus = parse_uint_const("SWAP_FEE_BONUS_VAULT_PCT", cls.cfg_text)
        cls.treasury = parse_uint_const("SWAP_FEE_TREASURY_SHARE_PCT", cls.cfg_text)
        cls.hearth_bps = parse_uint_const("HEARTH_FEE_BPS", cls.cfg_text)
        cls.pages = nav_page_paths()
        cls.page_texts = {p: p.read_text() for p in cls.pages}
        cls.blob = "\n".join(cls.page_texts.values())

    def test_config_matches_expected_product_truth(self):
        self.assertEqual(self.swap_bps, 100)
        self.assertEqual(self.cd, 69)
        self.assertEqual(self.bonus, 11)
        self.assertEqual(self.treasury, 20)
        self.assertEqual(self.hearth_bps, 100)
        self.assertEqual(self.cd + self.bonus + self.treasury, 100)

    def test_intro_states_heat_mint_then_cd(self):
        intro = (DOCS / "introduction.mdx").read_text()
        intro_plain = re.sub(r"[*_`#]", "", intro).lower()
        self.assertIn("mint heat", intro_plain)
        self.assertIn("mintheatv10", intro_plain)
        self.assertIn("lock heat", intro_plain)
        self.assertIn("cannot lock raw xfg as a cd", intro_plain)
        self.assertIn("69%", intro)
        self.assertIn("11%", intro)
        self.assertIn("20%", intro)

    def test_nav_docs_mention_live_fee_splits(self):
        for n in (self.cd, self.bonus, self.treasury):
            self.assertIn(f"{n}%", self.blob)
        self.assertIn("SWAP_FEE_RATE_BPS", self.blob)
        self.assertIn("HEARTH_FEE_BPS", self.blob)
        self.assertRegex(self.blob, r"50%\s*.{0,40}50%|50/50")

    def test_no_primary_nav_stark_heat_burns(self):
        cfg = json.loads(DOCS_JSON.read_text())
        groups = nav_groups(cfg)
        pages = [p for g in groups for p in g.get("pages", [])]
        self.assertFalse(any("heat-burns" in p for p in pages))
        digm_groups = [
            g.get("group", "")
            for g in groups
            if any("digm" in p for p in g.get("pages", []))
        ]
        for name in digm_groups:
            self.assertTrue(
                any(k in name.lower() for k in ("optional", "other", "digm")),
                msg=f"DIGM group should be optional-ish, got {name!r}",
            )

    def test_cd_pages_not_lock_xfg_as_primary(self):
        cd = (DOCS / "features/commitment-deposits/how-cds-work.mdx").read_text().lower()
        self.assertTrue(
            "there is **no** xfg cd" in cd or "no** xfg cd" in cd or "no xfg cd path" in cd,
            msg="CD page must deny XFG CD path",
        )
        self.assertNotRegex(cd, r"lock xfg for a (fixed )?term and earn")

    def test_all_nav_pages_free_of_residual_wrong_basics(self):
        """Scan every docs.json-linked page for residual false product claims."""
        failures: list[str] = []
        for path, text in self.page_texts.items():
            rel = path.relative_to(DOCS)
            for name, ctx in residual_hits(text):
                failures.append(f"{rel}: {name}: ...{ctx[:160]}...")
        if failures:
            self.fail(
                "Residual incorrect basics still present on nav-linked pages:\n"
                + "\n".join(failures)
            )

    def test_security_pages_heat_cd_and_hearth_fees(self):
        risk = (DOCS / "security/risk-warnings.mdx").read_text()
        risk_l = risk.lower()
        self.assertIn("heat", risk_l)
        self.assertIn("inaccessible until the maturity", risk_l)
        self.assertNotIn("your xfg is inaccessible until the maturity", risk_l)
        self.assertIn("hearth_fee_bps", risk_l)
        self.assertIn("1%", risk)
        self.assertIn("mintheatv10", risk_l)
        self.assertNotRegex(risk, r"\b0\.3%\s+fee\s+income\b", re.I)
        wallet = (DOCS / "security/wallet-safety.mdx").read_text()
        self.assertNotRegex(
            wallet,
            r"Generate\s+STARK\s+proofs?\s+for\s+HEAT\s+burns",
            re.I,
        )
        self.assertIn("mintHeatV10", wallet)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DocsFeeAlignment)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
