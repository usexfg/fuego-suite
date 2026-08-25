from typing import Dict, Tuple, Any
from .config import CDFeePoolConfig


class AtomicSwapAnalyzer:
    def __init__(self):
        self.swap_states = {
            10: "ADAPTOR_KEYS_EXCHANGED",
            11: "ADAPTOR_ESCROW_FUNDED",
            12: "ADAPTOR_PRESIGS_READY",
            13: "ADAPTOR_CTR_LOCKED",
            14: "ADAPTOR_SECRET_REVEALED",
            15: "ADAPTOR_XFG_SPENT",
            16: "ADAPTOR_REFUNDED",
        }

    def analyze_swap_fee(self, xfg_amount: int) -> Tuple[int, int]:
        config = CDFeePoolConfig()
        fee_amount = (xfg_amount * config.swap_fee_rate_bps) // config.swap_fee_rate_divisor
        net_amount = xfg_amount - fee_amount
        return fee_amount, net_amount

    def analyze_swap_state_transition(self, current_state: int, target_state: int) -> bool:
        valid_transitions = {
            10: [11],
            11: [12, 16],
            12: [13],
            13: [14, 16],
            14: [15],
            16: [],
        }
        return target_state in valid_transitions.get(current_state, [])

    def get_state_name(self, state_id: int) -> str:
        return self.swap_states.get(state_id, "UNKNOWN")