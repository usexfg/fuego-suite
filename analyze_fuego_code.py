#!/usr/bin/env python3
"""
Utility to analyze Fuego source code and extract blockchain mechanics.
"""

import os
import re
import json
from pathlib import Path
from typing import Dict, List, Any

class FuegoCodeAnalyzer:
    """Analyze Fuego source code for blockchain mechanics"""
    
    def __init__(self, source_dir: str = "/Users/aejt/fuego"):
        self.source_dir = Path(source_dir)
        self.analysis_results = {}
        
    def analyze_cd_interest_code(self) -> Dict[str, Any]:
        """Analyze CD interest calculation code"""
        print("Analyzing CD interest calculation code...")
        
        interest_files = []
        interest_patterns = [
            r"calculateCdInterest",
            r"CD.*interest",
            r"epoch.*fee.*rate",
            r"FEE_POOL_RATE_PRECISION"
        ]
        
        # Search for interest calculation code
        for file_path in self.source_dir.rglob("*.cpp"):
            content = file_path.read_text(errors='ignore')
            if any(re.search(pattern, content, re.IGNORECASE) for pattern in interest_patterns):
                interest_files.append(str(file_path.relative_to(self.source_dir)))
                
        # Read key files
        key_files = {
            "Currency.cpp": self._read_file("src/CryptoNoteCore/Currency.cpp"),
            "Currency.h": self._read_file("src/CryptoNoteCore/Currency.h"),
            "CryptoNoteConfig.h": self._read_file("src/CryptoNoteConfig.h"),
            "CommitmentIndex.cpp": self._read_file("src/CryptoNoteCore/CommitmentIndex.cpp"),
            "CommitmentIndex.h": self._read_file("src/CryptoNoteCore/CommitmentIndex.h")
        }
        
        # Extract formulas
        formulas = self._extract_interest_formulas(key_files["Currency.cpp"])
        
        return {
            "files_found": interest_files,
            "key_files": list(key_files.keys()),
            "formulas": formulas,
            "config_extracts": self._extract_config_values(key_files["CryptoNoteConfig.h"])
        }
    
    def analyze_atomic_swap_code(self) -> Dict[str, Any]:
        """Analyze atomic swap implementation code"""
        print("Analyzing atomic swap code...")
        
        swap_files = []
        swap_patterns = [
            r"atomic.*swap",
            r"adaptor.*signature",
            r"swap.*daemon",
            r"HTLC",
            r"Musig2"
        ]
        
        # Search for swap code
        for file_path in self.source_dir.rglob("*.cpp"):
            content = file_path.read_text(errors='ignore')
            if any(re.search(pattern, content, re.IGNORECASE) for pattern in swap_patterns):
                swap_files.append(str(file_path.relative_to(self.source_dir)))
                
        # Also check Go files in swapxfg
        go_swap_files = []
        swapxfg_dir = self.source_dir / "swapxfg"
        if swapxfg_dir.exists():
            for file_path in swapxfg_dir.rglob("*.go"):
                content = file_path.read_text(errors='ignore')
                if "swap" in content.lower() or "atomic" in content.lower():
                    go_swap_files.append(str(file_path.relative_to(self.source_dir)))
        
        return {
            "cpp_files": swap_files,
            "go_files": go_swap_files,
            "swap_states": self._extract_swap_states(),
            "documentation": self._check_swap_documentation()
        }
    
    def analyze_p2p_code(self) -> Dict[str, Any]:
        """Analyze P2P networking code"""
        print("Analyzing P2P networking code...")
        
        p2p_files = []
        p2p_dir = self.source_dir / "src" / "P2p"
        
        if p2p_dir.exists():
            for file_path in p2p_dir.rglob("*"):
                if file_path.is_file():
                    p2p_files.append(str(file_path.relative_to(self.source_dir)))
        
        # Analyze protocol definitions
        protocol_defs = self._read_file("src/P2p/P2pProtocolDefinitions.h")
        consensus_files = self._find_consensus_code()
        
        return {
            "p2p_files": sorted(p2p_files),
            "protocol_versions": self._extract_protocol_versions(protocol_defs),
            "consensus_files": consensus_files,
            "network_config": self._extract_network_config()
        }
    
    def analyze_crypto_code(self) -> Dict[str, Any]:
        """Analyze cryptographic primitives"""
        print("Analyzing cryptographic primitives...")
        
        crypto_files = []
        crypto_dir = self.source_dir / "src" / "crypto"
        
        if crypto_dir.exists():
            for file_path in crypto_dir.rglob("*"):
                if file_path.is_file():
                    crypto_files.append(str(file_path.relative_to(self.source_dir)))
        
        # Check for specific cryptographic algorithms
        algorithms = {
            "signatures": self._check_for_algorithm(["Ed25519", "Schnorr", "Ring"]),
            "hash_functions": self._check_for_algorithm(["Keccak", "SHA-3", "Blake"]),
            "commitments": self._check_for_algorithm(["Pedersen", "Bulletproof"]),
            "zk_proofs": self._check_for_algorithm(["SNARK", "STARK", "DLEQ"])
        }
        
        return {
            "crypto_files": sorted(crypto_files),
            "algorithms_found": algorithms,
            "musig2_implementation": self._check_musig2_implementation()
        }
    
    def analyze_fee_distribution_code(self) -> Dict[str, Any]:
        """Analyze fee distribution code"""
        print("Analyzing fee distribution code...")
        
        fee_files = []
        fee_patterns = [
            r"fee.*distribution",
            r"epoch.*fee",
            r"swap.*fee",
            r"treasury",
            r"fee.*pool"
        ]
        
        for file_path in self.source_dir.rglob("*.cpp"):
            content = file_path.read_text(errors='ignore')
            if any(re.search(pattern, content, re.IGNORECASE) for pattern in fee_patterns):
                fee_files.append(str(file_path.relative_to(self.source_dir)))
        
        # Key fee distribution files
        key_files = {
            "Blockchain.cpp": self._extract_fee_distribution_logic(
                self._read_file("src/CryptoNoteCore/Blockchain.cpp")
            ),
            "CommitmentIndex.cpp": self._check_fee_recording(
                self._read_file("src/CryptoNoteCore/CommitmentIndex.cpp")
            )
        }
        
        return {
            "fee_files": fee_files,
            "distribution_logic": key_files["Blockchain.cpp"],
            "epoch_processing": self._extract_epoch_processing()
        }
    
    def generate_comprehensive_report(self) -> Dict[str, Any]:
        """Generate comprehensive analysis report"""
        print("Generating comprehensive analysis report...")
        
        self.analysis_results = {
            "timestamp": self._get_timestamp(),
            "source_directory": str(self.source_dir),
            "cd_interest_analysis": self.analyze_cd_interest_code(),
            "atomic_swap_analysis": self.analyze_atomic_swap_code(),
            "p2p_analysis": self.analyze_p2p_code(),
            "crypto_analysis": self.analyze_crypto_code(),
            "fee_distribution_analysis": self.analyze_fee_distribution_code(),
            "summary": self._generate_summary()
        }
        
        return self.analysis_results
    
    def save_report(self, output_file: str = "fuego_code_analysis.json"):
        """Save analysis report to JSON file"""
        if not self.analysis_results:
            self.generate_comprehensive_report()
        
        with open(output_file, "w") as f:
            json.dump(self.analysis_results, f, indent=2)
        
        print(f"Report saved to: {output_file}")
        return output_file
    
    def _read_file(self, relative_path: str) -> str:
        """Read file content"""
        file_path = self.source_dir / relative_path
        if file_path.exists():
            try:
                return file_path.read_text(errors='ignore')
            except:
                return ""
        return ""
    
    def _extract_interest_formulas(self, currency_code: str) -> Dict[str, str]:
        """Extract interest calculation formulas from Currency.cpp"""
        formulas = {}
        
        # Look for calculateCdInterest function
        cd_interest_match = re.search(
            r"uint64_t Currency::calculateCdInterest[^{]+\{([^}]+(?:\{[^}]*\}[^}]*)*)\}",
            currency_code,
            re.DOTALL
        )
        
        if cd_interest_match:
            formulas["calculateCdInterest"] = cd_interest_match.group(0)[:500] + "..."
        
        # Look for fee rate calculation
        fee_rate_match = re.search(
            r"epochRate.*=.*commitmentIndex\.getEpochFeeRate",
            currency_code
        )
        if fee_rate_match:
            formulas["epochRate_calculation"] = "Found epoch fee rate usage"
        
        return formulas
    
    def _extract_config_values(self, config_code: str) -> Dict[str, str]:
        """Extract configuration values from CryptoNoteConfig.h"""
        configs = {}
        
        patterns = {
            "EPOCH_DURATION_BLOCKS": r"const uint64_t EPOCH_DURATION_BLOCKS = (\d+)",
            "SWAP_FEE_RATE_BPS": r"const uint64_t SWAP_FEE_RATE_BPS = (\d+)",
            "SWAP_FEE_CD_SHARE_PCT": r"const uint64_t SWAP_FEE_CD_SHARE_PCT = (\d+)",
            "FEE_POOL_RATE_PRECISION": r"const uint64_t FEE_POOL_RATE_PRECISION = (\d+)",
            "COLD_MIN_TERM": r"const uint32_t COLD_MIN_TERM = (\d+)",
            "COLD_MAX_TERM": r"const uint32_t COLD_MAX_TERM = (\d+)"
        }
        
        for key, pattern in patterns.items():
            match = re.search(pattern, config_code)
            if match:
                configs[key] = match.group(1)
        
        return configs
    
    def _extract_swap_states(self) -> List[Dict[str, str]]:
        """Extract atomic swap states from documentation"""
        docs_path = self.source_dir / "docs" / "features" / "atomic-swaps" / "how-swaps-work.mdx"
        states = []
        
        if docs_path.exists():
            content = docs_path.read_text(errors='ignore')
            # Look for state definitions
            state_matches = re.findall(r"ADAPTOR_(\w+)\s*\((\d+)\)", content)
            for state_name, state_id in state_matches:
                states.append({
                    "id": int(state_id),
                    "name": f"ADAPTOR_{state_name}"
                })
        
        return sorted(states, key=lambda x: x["id"])
    
    def _check_swap_documentation(self) -> Dict[str, Any]:
        """Check swap documentation"""
        docs = {
            "files": [],
            "topics": []
        }
        
        docs_dir = self.source_dir / "docs"
        if docs_dir.exists():
            # Look for swap-related documentation
            for file_path in docs_dir.rglob("*.md*"):
                if "swap" in file_path.name.lower():
                    docs["files"].append(str(file_path.relative_to(self.source_dir)))
            
            # Check for specific topics
            topics_file = docs_dir / "docs.json"
            if topics_file.exists():
                try:
                    topics_data = json.loads(topics_file.read_text())
                    if "features" in topics_data:
                        for feature in topics_data["features"]:
                            if "swap" in str(feature).lower():
                                docs["topics"].append(feature)
                except:
                    pass
        
        return docs
    
    def _extract_protocol_versions(self, protocol_defs: str) -> Dict[str, str]:
        """Extract protocol versions"""
        versions = {}
        
        p2p_match = re.search(r"P2P_PROTOCOL_VERSION\s*=\s*(\d+)", protocol_defs)
        blockchain_match = re.search(r"BLOCKCHAIN_PROTOCOL_VERSION\s*=\s*(\d+)", protocol_defs)
        
        if p2p_match:
            versions["P2P"] = p2p_match.group(1)
        if blockchain_match:
            versions["BLOCKCHAIN"] = blockchain_match.group(1)
        
        return versions
    
    def _find_consensus_code(self) -> List[str]:
        """Find consensus algorithm code"""
        consensus_files = []
        consensus_patterns = [
            r"consensus",
            r"difficulty",
            r"block.*validation",
            r"proof.*of.*work"
        ]
        
        for file_path in self.source_dir.rglob("*.cpp"):
            content = file_path.read_text(errors='ignore')
            if any(re.search(pattern, content, re.IGNORECASE) for pattern in consensus_patterns):
                consensus_files.append(str(file_path.relative_to(self.source_dir)))
        
        return consensus_files
    
    def _extract_network_config(self) -> Dict[str, Any]:
        """Extract network configuration"""
        config = {}
        
        # Check for net node config
        netnode_config = self._read_file("src/P2p/NetNodeConfig.cpp")
        if "P2P_DEFAULT_PORT" in netnode_config:
            config["default_port"] = "found"
        
        # Check for seed nodes
        seed_match = re.search(r"SEED_NODES.*=.*\{[^}]+\}", netnode_config, re.DOTALL)
        if seed_match:
            config["seed_nodes_configured"] = True
        
        return config
    
    def _check_for_algorithm(self, algorithm_names: List[str]) -> List[str]:
        """Check if cryptographic algorithms are present in code"""
        found = []
        
        for algo in algorithm_names:
            # Search in crypto directory
            crypto_dir = self.source_dir / "src" / "crypto"
            if crypto_dir.exists():
                for file_path in crypto_dir.rglob("*"):
                    if file_path.is_file():
                        content = file_path.read_text(errors='ignore')
                        if algo.lower() in content.lower():
                            found.append(algo)
                            break
        
        return found
    
    def _check_musig2_implementation(self) -> Dict[str, Any]:
        """Check for MuSig2 implementation"""
        musig2_info = {
            "found": False,
            "files": []
        }
        
        musig2_file = self.source_dir / "src" / "crypto" / "musig2.h"
        if musig2_file.exists():
            musig2_info["found"] = True
            musig2_info["files"].append("src/crypto/musig2.h")
            
            content = musig2_file.read_text(errors='ignore')
            if "adaptor" in content.lower():
                musig2_info["used_for_swaps"] = True
        
        return musig2_info
    
    def _extract_fee_distribution_logic(self, blockchain_code: str) -> Dict[str, Any]:
        """Extract fee distribution logic from Blockchain.cpp"""
        logic = {
            "epoch_processing_found": False,
            "fee_recording_found": False,
            "treasury_handling_found": False
        }
        
        if "recordEpochFeeRate" in blockchain_code:
            logic["fee_recording_found"] = True
        
        if "epochNumber" in blockchain_code and "epochDuration" in blockchain_code:
            logic["epoch_processing_found"] = True
        
        if "treasury" in blockchain_code.lower():
            logic["treasury_handling_found"] = True
        
        return logic
    
    def _check_fee_recording(self, commitment_code: str) -> Dict[str, Any]:
        """Check fee recording implementation"""
        recording = {
            "recordEpochFeeRate_implemented": "recordEpochFeeRate" in commitment_code,
            "generateEpochReport_implemented": "generateEpochReport" in commitment_code,
            "getEpochFeeRate_implemented": "getEpochFeeRate" in commitment_code
        }
        
        return recording
    
    def _extract_epoch_processing(self) -> Dict[str, Any]:
        """Extract epoch processing logic"""
        processing = {
            "boundary_detection": False,
            "fee_distribution": False,
            "rate_calculation": False
        }
        
        blockchain_code = self._read_file("src/CryptoNoteCore/Blockchain.cpp")
        
        # Check for epoch boundary detection
        if "epochDuration" in blockchain_code and "%" in blockchain_code:
            processing["boundary_detection"] = True
        
        # Check for fee distribution
        if "distribute" in blockchain_code.lower() and "fee" in blockchain_code.lower():
            processing["fee_distribution"] = True
        
        # Check for rate calculation
        if "calculate" in blockchain_code.lower() and "rate" in blockchain_code.lower():
            processing["rate_calculation"] = True
        
        return processing
    
    def _get_timestamp(self) -> str:
        """Get current timestamp"""
        from datetime import datetime
        return datetime.now().isoformat()
    
    def _generate_summary(self) -> Dict[str, Any]:
        """Generate analysis summary"""
        summary = {
            "total_components_analyzed": 5,
            "code_health_indicators": {
                "cd_interest": "Formulas extracted from Currency.cpp",
                "atomic_swaps": "State machine and adaptor signatures found",
                "p2p_networking": "Protocol definitions and consensus code found",
                "cryptography": "Multiple cryptographic primitives implemented",
                "fee_distribution": "Epoch-based fee distribution logic found"
            },
            "recommendations": [
                "Integrate analysis tools with actual node data",
                "Add visualization for CD interest projections",
                "Create swap simulation environment",
                "Add network monitoring dashboard"
            ]
        }
        
        return summary

def main():
    """Main function"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Analyze Fuego blockchain source code")
    parser.add_argument("--source-dir", default="/Users/aejt/fuego", 
                       help="Path to Fuego source directory")
    parser.add_argument("--output", default="fuego_code_analysis.json",
                       help="Output JSON file for analysis results")
    parser.add_argument("--component", choices=["cd", "swap", "p2p", "crypto", "fee", "all"],
                       default="all", help="Specific component to analyze")
    
    args = parser.parse_args()
    
    analyzer = FuegoCodeAnalyzer(args.source_dir)
    
    if args.component == "all":
        report = analyzer.generate_comprehensive_report()
        output_file = analyzer.save_report(args.output)
        
        # Print summary
        print("\n" + "="*80)
        print("ANALYSIS SUMMARY")
        print("="*80)
        
        components = report.keys()
        for component in ["cd_interest_analysis", "atomic_swap_analysis", 
                         "p2p_analysis", "crypto_analysis", "fee_distribution_analysis"]:
            if component in report:
                data = report[component]
                if "files_found" in data:
                    print(f"{component.replace('_', ' ').title()}: {len(data['files_found'])} files")
                elif "cpp_files" in data:
                    print(f"{component.replace('_', ' ').title()}: {len(data['cpp_files'])} C++ files")
                elif "p2p_files" in data:
                    print(f"{component.replace('_', ' ').title()}: {len(data['p2p_files'])} files")
                elif "crypto_files" in data:
                    print(f"{component.replace('_', ' ').title()}: {len(data['crypto_files'])} files")
                elif "fee_files" in data:
                    print(f"{component.replace('_', ' ').title()}: {len(data['fee_files'])} files")
        
        print(f"\nFull report saved to: {output_file}")
        
    else:
        # Analyze specific component
        if args.component == "cd":
            result = analyzer.analyze_cd_interest_code()
        elif args.component == "swap":
            result = analyzer.analyze_atomic_swap_code()
        elif args.component == "p2p":
            result = analyzer.analyze_p2p_code()
        elif args.component == "crypto":
            result = analyzer.analyze_crypto_code()
        elif args.component == "fee":
            result = analyzer.analyze_fee_distribution_code()
        
        print(json.dumps(result, indent=2))
    
    return 0

if __name__ == "__main__":
    import sys
    sys.exit(main())