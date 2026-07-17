# DIGM CD Real Yield Strategy Design Document

## 1. Understanding & Context

### Goal
Provide robust and diversified "real yield" for DIGM Certificates of Deposit (CDs), similar to HEAT CDs, explicitly without relying on inflationary token emissions. This aims to enhance DIGM's utility as a stablecoin and contribute to the overall Fuego ecosystem's value.

### Context
DIGM is a stablecoin pegged 1:10 to HEAT on Fuego L1. Its primary utility is within a music album marketplace and streaming radio station application.

### Target Audience
DIGM CD holders (for yield) and the broader Fuego ecosystem (through increased fee generation and value capture).

### Core L1 Features (Existing)
*   **Fixed-Rate Swaps:** HEAT/DIGM primary pool with a 0.3% fee.
*   **Minting:** DIGM minting from HEAT via `DigmMintEngine`.
*   **Cross-chain (Future):** Constants exist (`DIGM_LOCK_TERM`, `DIGM_UNLOCK_TERM`, `STARK_TARGET_CHAIN_BASE`) for a STARK bridge, but no implementation yet.

### Key Constraints
*   **"Real Yield" Only:** Yield must be derived from actual economic activity, fees, and value creation; no inflationary token emissions.
*   **L1 Anchor:** DIGM is anchored to 0.1 HEAT on Fuego L1.

### Explicit Non-Goals
*   Generating yield through inflationary token printing.

## 2. Assumptions

*   Fuego L1 (HEAT) has robust protocol revenue generation that HEAT CDs currently tap into.
*   The DIGM music platform (marketplace/radio) will generate significant economic activity.
*   Cross-chain bridge functionality for DIGM will eventually be implemented and operational.
*   Users are willing to lock DIGM in CDs for yield.

## 3. Open Questions (for further investigation)

*   **HEAT CD Revenue Streams:** A detailed breakdown of the specific sources for HEAT CD yield is needed to ensure seamless integration and appropriate allocation for DIGM CDs.
*   **DIGM Volumes/TVL:** Projections for DIGM transaction volumes, sales on the music platform, and potential Total Value Locked (TVL) in CDs/lending pools are crucial for sizing the yield potential and optimizing fee structures.
*   **Music Platform Monetization Model:** A clear and granular understanding of the DIGM music platform's specific fee structures and revenue models is essential for accurately calculating the portion allocated to DIGM CDs.

## 4. Decision Log

*   **Decision (Strategy Choice):** Proceed with a consolidated strategy for DIGM CD real yield.
    *   **Alternatives Considered:** Focusing on individual approaches (Platform Fee Share; Protocol-Owned Liquidity & Lending; NFT Royalties & Content Creator Fees).
    *   **Reasoning:** A consolidated approach leverages multiple synergistic revenue streams, maximizing real yield generation and ecosystem integration, aligning best with the goal of a robust multi-chain stablecoin.

## 5. Consolidated Strategy for DIGM CD Real Yield

This multi-faceted strategy integrates revenue streams from the Fuego L1, the DIGM music platform, and future DeFi/cross-chain functionalities to create a robust and diversified "real yield" mechanism for DIGM Certificates of Deposit.

### 5.1. I. Fuego L1 & Core Protocol Fees (Baseline Integration)

*   **Source:**
    *   **DIGM/HEAT Primary Pool Swap Fees:** The 0.3% fee collected from every HEAT/DIGM swap on Fuego L1.
    *   **HEAT CD Protocol Revenue Share:** A portion of the overall Fuego L1 protocol revenue (e.g., general network transaction fees, block rewards not from inflation, future network resource fees) that is currently allocated to HEAT CDs will also be partially allocated to DIGM CDs. This creates a direct link to the broader Fuego L1's success.
*   **Mechanism:** These fees are collected by the Fuego L1 protocol. A defined percentage of the *net fees* (after any operational costs) will be periodically funneled into a distribution pool for DIGM CD holders.
*   **Real Yield Contribution:** Direct share of core L1 utility and liquidity provision.

### 5.2. II. DIGM Music Platform Fees (Direct Application Revenue)

*   **Source:** A defined percentage of all transaction fees and revenue streams generated *directly within the DIGM music album marketplace and streaming radio station*. This could include:
    *   **Album/Track Sales Fees:** A commission on every music purchase.
    *   **Premium Streaming Subscriptions:** Fees for ad-free streaming, high-fidelity audio, or exclusive content access.
    *   **Artist Services Fees:** Fees for premium tools, promotional features, or enhanced analytics offered to artists.
    *   **Advertising Revenue:** If the streaming radio includes ad-supported tiers, a share of ad revenue.
    *   **Music NFT Royalties:** A percentage of secondary sales royalties for music NFTs issued or traded on the platform (if implemented).
*   **Mechanism:** These fees, collected in DIGM or converted to DIGM, are aggregated by the platform. A pre-determined portion of these net fees is then added to the distribution pool for DIGM CD stakers.
*   **Real Yield Contribution:** Directly reflects the economic success and user engagement of the DIGM music platform.

### 5.3. III. Future DeFi & Cross-Chain Integration Fees

*   **Source:** As the Fuego ecosystem and DIGM mature, additional fees can be channeled into the DIGM CD yield.
    *   **Cross-Chain Bridge Fees:** Once the STARK bridge for DIGM is implemented, fees incurred for bridging DIGM to and from other chains (e.g., Ethereum, Base) would contribute. These fees would cover operational costs and security, with a portion allocated to DIGM CD holders.
    *   **DIGM Lending/Borrowing Interest (DeFi):** If a dedicated lending market for DIGM is established on Fuego L1 or a connected chain, a share of the interest paid by borrowers of DIGM could be directed to DIGM CD stakers.
    *   **Protocol-Owned Liquidity (POL) on Fuego L1:** If the Fuego protocol (or a DAO) establishes and manages DIGM/HEAT liquidity within the primary pool, the swap fees generated by this POL could be allocated to DIGM CD holders.
*   **Mechanism:** These future fee streams, once operational, will be integrated into the existing fee aggregation and distribution mechanism, further diversifying and strengthening the real yield for DIGM CD stakers.
*   **Real Yield Contribution:** Expands yield sources beyond the core L1 and music platform, leveraging broader DeFi and interoperability.

## 6. Overall Advantages of this Consolidated Strategy

*   **Diversified Real Yield:** Multiple, independent revenue streams reduce reliance on any single source, enhancing stability and predictability of yield.
*   **Strong Economic Alignment:** Yield is directly tied to the usage and success of Fuego L1, the DIGM music platform, and future cross-chain / DeFi integrations.
*   **Non-Inflationary:** Strictly adheres to the "real yield" principle, avoiding token inflation.
*   **Enhanced DIGM Utility:** Provides a powerful incentive to hold, use, and stake DIGM, strengthening its stablecoin peg and overall market confidence.
*   **Fuego Ecosystem Value:** Directly channels value generated across various Fuego sub-ecosystems back into the core stablecoin.
