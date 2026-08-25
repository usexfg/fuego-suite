"""Fuego Currency Expert - CD interest, deposits, tokenomics."""

class CurrencyExpert:
    """Domain expert for Fuego currency/deposits."""
    
    def __init__(self, source_dir: str = "/Users/aejt/fuego"):
        self.source_dir = source_dir
        self.is_testnet = False
    
    COIN = 10000000  # 10^7 atomic units
    MAX_SUPPLY = 80000088000008  # 8M8
    EPOCH_BLOCKS = 900  # ~5 days
    DEPOSIT_MIN = 8000000  # 0.8 XFG
    DEPOSIT_MIN_TERM = 16440  # 3 months
    
    def calculate_cd_interest(self, amount: int, creation_height: int, 
                            current_height: int, epoch_fee_rates: list) -> int:
        """Calculate CD interest for amount."""
        epochs = (current_height - creation_height) // self.EPOCH_BLOCKS
        if epochs <= 0:
            return 0
        
        total = 0
        for rate in epoch_fee_rates[:epochs]:
            total += amount * rate // 1000000
        return total
    
    def estimate_apy(self, epoch_swap_fees: int, total_cd_locked: int) -> float:
        """Estimate APY based on epoch fees and CD locked."""
        if total_cd_locked == 0:
            return 0.0
        # APY = (0.8 × fees × 73) / cd_locked × 100%
        return (0.8 * epoch_swap_fees * 73) / total_cd_locked * 100
    
    def get_deposit_info(self, deposit_type: str = "COLD") -> dict:
        """Get deposit type information."""
        if deposit_type == "HEAT":
            return {"type": "HEAT", "term": "forever", "min": self.DEPOSIT_MIN}
        return {"type": "COLD", "term": "3mo-1yr", "min": self.DEPOSIT_MIN, 
                "min_term": self.DEPOSIT_MIN_TERM}


__all__ = ["CurrencyExpert"]