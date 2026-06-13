# Mission Plan: mission_swapxfg_guide

## Objective
Create the definitive user guide for SWAPXFG.

## Phase 1: High Council (Complete)
- [x] Research SWAPXFG functionality and flags.
- [x] Define AIM specification (`spec.intent`).
- [ ] Present Plan to Tuke.

## Phase 2: The Forge (Implementation)
- [ ] Draft Section 1: Introduction & Philosophy.
- [ ] Draft Section 2: Getting Started (Installation, Flags).
- [ ] Draft Section 3: The Terminal Interface (Pairs, Orderbook).
- [ ] Draft Section 4: External Connectivity (Bridges, BCH).
- [ ] Draft Section 5: Swap Operations (Soft Orders vs AFK Locks).
- [ ] Finalize `docs/tutorials/SWAPXFG_USER_GUIDE.md`.

## Phase 3: The Sentinel (Adversarial Audit)
- [ ] Security Review: Verify bridge port safety and RPC credential handling.
- [ ] Privacy Check: Ensure no IP leakage or address linkability in swap flows.
- [ ] Documentation Audit: Cross-check every command against `tui.go`.

## Phase 4: The Altar (Verification & Integration)
- [ ] Format Verification (Markdown lint).
- [ ] Link Verification.
- [ ] Sacred Commit to master.

## Recommendations
- Use Mermaid diagrams to visualize the Adaptor Signature / HTLC swap lifecycle.
- Highlight the "Privacy First" min ring size 8 requirement for XFG.
