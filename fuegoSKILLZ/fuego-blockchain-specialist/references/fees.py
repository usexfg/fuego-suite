from typing import Dict, Any
from .config import CDFeePoolConfig


class FeeDistributionAnalyzer:
    def __init__(self):
        self.config = CDFeePoolConfig()

    def analyze_epoch_distribution(
        self, epoch_swap_fees: int, total_cd_locked: int
    ) -> Dict[str, Any]:
        cd_share = (epoch_swap_fees * self.config.cd_share_pct) // 100
        treasury_share = (epoch_swap_fees * self.config.treasury_share_pct) // 100
        fee_rate = 0
        if total_cd_locked > 0:
            fee_rate = (cd_share * self.config.fee_pool_rate_precision) // total_cd_locked
        return {
            "epoch_swap_fees": epoch_swap_fees,
            "cd_share": cd_share,
            "treasury_share": treasury_share,
            "fee_rate": fee_rate,
            "cd_share_percentage": self.config.cd_share_pct,
            "treasury_share_percentage": self.config.treasury_share_pct,
            "distribution_note": f"{self.config.cd_share_pct}% to CD holders, {self.config.treasury_share_pct}% to treasury",
        }