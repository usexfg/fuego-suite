# Implementation Plan: Sovereign UX (The Flame Refactor)

## Objective
Simplify and clean the SWAPXFG UI/UX to make it intuitive and "vibe-coded" for elite sovereign use.

## Phase 1: High Council (Specification)
- **Status**: [x] Defined AIM v2.2 intent.
- **Tasks**:
  - [ ] Finalize sidebar layout wireframes.
  - [ ] Define command completion logic for the Omnibar.

## Phase 2: Forge-Go (Implementation)
- **Status**: [ ] Pending.
- **Tasks**:
  - [ ] **Unified Command Engine**: Merge `offer` and `confirm-offer`. Create the `trade` orchestration logic.
  - [ ] **Omnibar UI**: Implement the real-time suggestion bar using `bubbletea` state tracking.
  - [ ] **Sidebar Component**: Add a persistent vertical navigation element.
  - [ ] **Responsive Grid**: Migration to `lipgloss` dynamic layout.

## Phase 3: The Sentinel (Adversarial Audit)
- **Status**: [ ] Pending.
- **Tasks**:
  - [ ] **Information Leakage Check**: Verify suggestions don't persist sensitive trade intents.
  - [ ] **Confirmation Security**: Ensure the (Y/N) prompt is not easily bypassable.

## Phase 4: The Altar (Sanctification)
- **Status**: [ ] Pending.
- **Tasks**:
  - [ ] Full build verification on ARM64.
  - [ ] Integration into `master`.

## Recommendations
- Use `bubbles/textinput` with enhanced completion handlers for the Omnibar.
- Prioritize visual feedback (color shifts) for successful bridge connections.
