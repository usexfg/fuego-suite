# Fuego Release Readiness Check

**Date**: 2026-10-22
**Version (version.h.in)**: 1.11.00 "Heatwave" (build 1175)
**Branch**: master (1 commit ahead of origin/master)
**Verdict**: **NO-GO** — 3 blockers remain (MCP vulns, dev-fund view-secret comment, disk); release notes resolved this session. Core is solid.

## Hard gates

| Gate | Status | Evidence |
|------|--------|----------|
| 0 Git state | PASS (clean) | `git status --porcelain` empty; 1 commit ahead of origin |
| 1 Disk space | PASS | 9.0 GiB free (96% used) — above 10 GiB? marginal; OK for incremental |
| 2 C++ build + tests | PASS | `make build-release` exit 0; ctest 53/55 (2 pre-existing live-e2e); core_tests 246/246; key_image 12/12 |
| 3 Go components | PASS | tui/swapxfg/dashboard: build+vet+test all clean |
| 4 Rust adaptor | PASS | cargo build + cargo test: 6/6 passed |
| 5 Solidity contracts | PASS | forge test: 15/15 passed |
| 6 MCP server | PASS (with caveat) | npm install + tsc build exit 0; 9 npm vulnerabilities (1 critical) |
| 7 Security (C/H/M) | PASS | 0 open CRITICAL/HIGH/MEDIUM (per SECURITY_AUDIT_FINDINGS.md) |
| 8 Version & release notes | PASS | `docs/releasenotes/RELEASE_NOTES_V11.md` created (v1.11.00 "HEATWAVE") |

## Soft gaps / warnings

1. **Unmerged security branches** (3): `security/audit-fixes-2026-10`,
   `security/audit-fixes-round2`, `claude/fuego-atomic-swaps-audit-9vq34l`.
   Both `security/*` branches are stale (based on older commits, delete tests/files) —
   do NOT merge wholesale. Cherry-pick only on owner request.
2. **Open LOW/INFO findings**: L-2b (wallet cipher, deferred), R2-3 (epoch fee dust,
   still applies to the 80/20 v11 split), R2-5 (early return from mutating epoch work),
   I-2 (popen PATH hijack, optional), I-5 (epoch-mint dual paths, deferred round 3).
   All LOW/INFO, not blockers.
3. **R2-4 (process)**: `fuego-currency` skill doc stale (80/20 vs 69/11/20) —
   **RESOLVED**: code is now 80/20 at v11+ and the `fuego-currency`, `fuego-swaps`,
   and `fuego-heat-and-hearth` skills were corrected to match.
4. **Dev-fund view secret** in `src/CryptoNoteConfig.h:586` — classified as a
   transparency feature (view-only, cannot spend). In-source comment calls it a launch
   blocker; audit doc says it is not a vulnerability. Disagreement unresolved; default
   = non-blocker (view-only). Owner decision needed to close the comment.
5. **MCP npm vulnerabilities** (9, incl. 1 critical) — dev tooling, not a shipped
   binary; flag for `npm audit fix` before release.
6. **Release notes** — `docs/releasenotes/RELEASE_NOTES_V11.md` created this session.
7. **ctest**: 2 failures are pre-existing live-node harnesses (`test_sol_e2e`,
   `test_xmr_live_e2e`, "need mode + args") — not regressions, do not block.
8. **Disk**: 9.0 GiB free is tight; a full clean rebuild may fail. Free space before
   a release build.

## Blockers (NO-GO)

1. ~~No release notes~~ — RESOLVED: `docs/releasenotes/RELEASE_NOTES_V11.md` created.
2. **MCP npm vulnerabilities** — run `npm audit fix` in `mcp/fuego-mcp-server/` and
   rebuild; at minimum, document the accepted risk.
3. **Dev-fund view-secret comment vs. audit classification** — reconcile: either
   remove the "LAUNCH BLOCKER" comment (audit says non-vuln) or implement the
   build-time injection (audit item 3). Owner decision required.
4. **Disk headroom** — free >= 10 GiB (or build on CI) before a clean release build.

## Recommended next action

1. Free disk space (>= 10 GiB).
2. Run `npm audit fix` in `mcp/fuego-mcp-server/` and rebuild.
3. Reconcile the dev-fund view-secret launch-blocker comment with the audit finding.
4. Re-run this check (`fuego-release-readiness` skill) — expect GO if the above close.
5. Tag `v1.11.00-Heatwave` only after GO.

## Evidence summary
- ctest: 53/55 passed (2 pre-existing live-e2e)
- core_tests: 246/246 passed
- key_image_uniqueness_tests: 12/12 passed
- Go: tui ok, swapxfg app ok, dashboard ok (all vet+test clean)
- Rust: 6/6 tests passed
- Solidity: 15/15 tests passed
- MCP: tsc build exit 0 (9 npm vulns)
- Git: clean, 1 commit ahead of origin
- Security: 0 open CRITICAL/HIGH/MEDIUM; 5 open LOW/INFO (deferred/documented)

## Skill
This check is encoded as the `fuego-release-readiness` skill
(`~/.config/opencode/skills/fuego-release-readiness/SKILL.md`). Re-run it before
any future tag.
