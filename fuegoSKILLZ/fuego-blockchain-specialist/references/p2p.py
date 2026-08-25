from typing import Dict, Any


class P2PConsensusAnalyzer:
    def __init__(self):
        self.protocol_versions = {"P2P": 1, "BLOCKCHAIN": 1}

    def analyze_peer_connections(self, peer_count: int, target_count: int = 8) -> Dict[str, Any]:
        health_score = min(peer_count / target_count, 1.0)
        if peer_count == 0:
            status = "DISCONNECTED"
        elif peer_count < target_count // 2:
            status = "UNDERCONNECTED"
        elif peer_count < target_count:
            status = "HEALTHY"
        else:
            status = "WELL_CONNECTED"
        return {
            "peer_count": peer_count,
            "target_count": target_count,
            "health_score": health_score,
            "status": status,
            "recommendation": self._get_connection_recommendation(peer_count, target_count),
        }

    def _get_connection_recommendation(self, peer_count: int, target_count: int) -> str:
        if peer_count == 0:
            return "No connections. Check network configuration and firewall settings."
        elif peer_count < 4:
            return f"Only {peer_count} connections. Consider adding more seed nodes or checking peer discovery."
        elif peer_count < target_count:
            return f"{peer_count}/{target_count} connections. Network is operational but could be more resilient."
        else:
            return f"{peer_count}/{target_count} connections. Network is well-connected."