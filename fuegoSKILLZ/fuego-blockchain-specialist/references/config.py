from dataclasses import dataclass


@dataclass
class CDFeePoolConfig:
    epoch_duration_blocks: int = 900
    testnet_epoch_duration_blocks: int = 10
    swap_fee_rate_bps: int = 100
    swap_fee_rate_divisor: int = 10000
    fee_pool_rate_precision: int = 1000000
    cd_share_pct: int = 80
    treasury_share_pct: int = 20
    cd_transfer_min_remaining_term: int = 1


@dataclass
class CDConfig:
    deposit_min_amount: int = 8000000
    cold_min_term: int = 16000
    cold_max_term: int = 65000
    testnet_cold_min_term: int = 8
    testnet_cold_max_term: int = 42
    deposit_term_forever: int = 4294967295