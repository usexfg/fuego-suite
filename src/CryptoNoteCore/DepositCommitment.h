// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// REMOVED: all STARK / commitment-generator types.
//   - StarkCommitmentGenerator (v3 xfg-stark-cli relay): no callers.
//     HEAT burns use mintHeatV10() + TX_EXTRA_HEAT_MINT_AUTH (0xF5).
//   - DepositCommitmentGenerator / DepositCommitment / CommitmentType
//     (0x08 HEAT + 0x07 YIELD extras): retired, no new extras created.
// Historical 0x08 extras remain parseable via TransactionExtra.h for the
// banking-index burn tally. See DEPOSIT_ARCHITECTURE.md.

#pragma once
