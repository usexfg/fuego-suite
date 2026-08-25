from typing import List
from .config import CDFeePoolConfig


class CDInterestCalculator:
    def __init__(self, is_testnet: bool = False):
        self.config = CDFeePoolConfig()
        self.is_testnet = is_testnet

    def calculate_epoch_fee_rate(self, epoch_swap_fees: int, total_cd_locked: int) -> int:
        if total_cd_locked == 0:
            return 0
        cd_share = epoch_swap_fees * self.config.cd_share_pct // 100
        fee_rate = (cd_share * self.config.fee_pool_rate_precision) // total_cd_locked
        max_uint64 = 2**64 - 1
        if fee_rate > max_uint64:
            raise ValueError(f"Fee rate overflow: {fee_rate} > {max_uint64}")
        return fee_rate

    def calculate_cd_interest(
        self, amount: int, creation_height: int, current_height: int, epoch_fee_rates: List[int]
    ) -> int:
        if current_height <= creation_height:
            return 0
        epoch_duration = (
            self.config.testnet_epoch_duration_blocks
            if self.is_testnet
            else self.config.epoch_duration_blocks
        )
        start_epoch = creation_height // epoch_duration
        end_epoch = current_height // epoch_duration
        interest = 0
        for epoch in range(start_epoch, min(end_epoch + 1, len(epoch_fee_rates))):
            epoch_rate = epoch_fee_rates[epoch]
            interest += (amount * epoch_rate) // self.config.fee_pool_rate_precision
        return interest

    def estimate_apy(self, current_epoch_fee: int, total_cd_locked: int) -> float:
        if total_cd_locked == 0:
            return 0.0
        epochs_per_year = 73
        cd_share = current_epoch_fee * self.config.cd_share_pct // 100
        annual_interest = cd_share * epochs_per_year
        apy = (annual_interest / total_cd_locked) * 100
        return apy