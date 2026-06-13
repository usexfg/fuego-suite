# Mission: Sovereign UX — SWAPXFG Simplification

Tuke, this document outlines the strategy for evolving SWAPXFG from a complex terminal into a clean, "vibe-coded" sovereign financial interface.

## 1. Unified Command Orchestration
Eliminate redundant preview/confirm steps. A single `swap <amount>` command will trigger an interactive TUI confirmation.

## 2. The Omnibar
A context-aware input bar at the bottom that provides real-time completions and suggestions.
- **Example**: Typing `1` suggests `swap 1 XFG` if a market is active.

## 3. Persistent Sidebar Navigation
Move away from a rotating pair list to a clearly defined hierarchy:
- **[M] Markets**: Trading terminal.
- **[C] CD Market**: Confidential Deposit secondary trading.
- **[S] Status**: Node and Network health.

## 4. Automatic Bridge Handshakes
The terminal will auto-detect local bridge availability (MetaMask/Phantom) and prompt for connection when relevant pairs are selected.

---

*Status: Approved by High Council. Implementation pending Forge-Go dispatch.*
