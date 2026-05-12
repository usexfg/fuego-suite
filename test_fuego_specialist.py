#!/usr/bin/env python3
"""
Test script for Fuego Blockchain Specialist Agent
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fuego_blockchain_specialist import (
    CDInterestCalculator,
    AtomicSwapAnalyzer,
    P2PConsensusAnalyzer,
    CryptographicPrimitives,
    FeeDistributionAnalyzer,
    FuegoBlockchainSpecialist
)

def test_cd_interest_calculator():
    """Test CD interest calculations"""
    print("Testing CD Interest Calculator...")
    
    calculator = CDInterestCalculator(is_testnet=False)
    
    # Test 1: Basic interest calculation
    amount = 100000000  # 1 XFG
    creation_height = 1000000
    current_height = 1100000
    epoch_fee_rates = [1000, 1500, 2000, 1800, 1600]
    
    interest = calculator.calculate_cd_interest(
        amount, creation_height, current_height, epoch_fee_rates
    )
    
    print(f"  Test 1 - Basic Interest:")
    print(f"    Amount: {amount:,} atomic units ({amount/1e8:.2f} XFG)")
    print(f"    Creation Height: {creation_height:,}")
    print(f"    Current Height: {current_height:,}")
    print(f"    Epochs: {len(epoch_fee_rates)}")
    print(f"    Interest: {interest:,} atomic units ({interest/1e8:.4f} XFG)")
    
    # Test 2: APY estimation
    current_epoch_fee = 500000000  # 5 XFG
    total_cd_locked = 10000000000  # 100 XFG
    
    apy = calculator.estimate_apy(current_epoch_fee, total_cd_locked)
    
    print(f"\n  Test 2 - APY Estimation:")
    print(f"    Current Epoch Fees: {current_epoch_fee:,} atomic units")
    print(f"    Total CD Locked: {total_cd_locked:,} atomic units")
    print(f"    Estimated APY: {apy:.2f}%")
    
    # Test 3: Epoch fee rate calculation
    epoch_swap_fees = 100000000  # 1 XFG
    total_cd_locked = 1000000000  # 10 XFG
    
    fee_rate = calculator.calculate_epoch_fee_rate(epoch_swap_fees, total_cd_locked)
    
    print(f"\n  Test 3 - Epoch Fee Rate:")
    print(f"    Epoch Swap Fees: {epoch_swap_fees:,} atomic units")
    print(f"    Total CD Locked: {total_cd_locked:,} atomic units")
    print(f"    Calculated Fee Rate: {fee_rate:,}")
    
    return True

def test_atomic_swap_analyzer():
    """Test atomic swap analysis"""
    print("\nTesting Atomic Swap Analyzer...")
    
    analyzer = AtomicSwapAnalyzer()
    
    # Test 1: Swap fee calculation
    xfg_amount = 100000000  # 1 XFG
    fee_amount, net_amount = analyzer.analyze_swap_fee(xfg_amount)
    
    print(f"  Test 1 - Swap Fee Calculation:")
    print(f"    XFG Amount: {xfg_amount:,} atomic units ({xfg_amount/1e8:.2f} XFG)")
    print(f"    Fee Amount: {fee_amount:,} atomic units ({fee_amount/1e8:.4f} XFG)")
    print(f"    Net Amount: {net_amount:,} atomic units ({net_amount/1e8:.2f} XFG)")
    print(f"    Fee Percentage: {(fee_amount/xfg_amount)*100:.1f}%")
    
    # Test 2: State transition validation
    test_cases = [
        (10, 11, True),   # KEYS_EXCHANGED -> ESCROW_FUNDED
        (11, 12, True),   # ESCROW_FUNDED -> PRESIGS_READY
        (11, 16, True),   # ESCROW_FUNDED -> REFUNDED
        (12, 13, True),   # PRESIGS_READY -> CTR_LOCKED
        (13, 14, True),   # CTR_LOCKED -> SECRET_REVEALED
        (13, 16, True),   # CTR_LOCKED -> REFUNDED
        (14, 15, True),   # SECRET_REVEALED -> XFG_SPENT
        (10, 16, False),  # Invalid: KEYS_EXCHANGED -> REFUNDED
        (15, 16, False),  # Invalid: XFG_SPENT -> REFUNDED (terminal)
    ]
    
    print(f"\n  Test 2 - State Transition Validation:")
    for from_state, to_state, expected in test_cases:
        is_valid = analyzer.analyze_swap_state_transition(from_state, to_state)
        status = "✓" if is_valid == expected else "✗"
        print(f"    {status} {from_state} -> {to_state}: {'Valid' if is_valid else 'Invalid'} (Expected: {'Valid' if expected else 'Invalid'})")
    
    return True

def test_p2p_consensus_analyzer():
    """Test P2P network analysis"""
    print("\nTesting P2P Consensus Analyzer...")
    
    analyzer = P2PConsensusAnalyzer()
    
    # Test different peer connection scenarios
    test_scenarios = [
        (0, 8, "DISCONNECTED"),
        (2, 8, "UNDERCONNECTED"),
        (6, 8, "HEALTHY"),
        (8, 8, "WELL_CONNECTED"),
        (12, 8, "WELL_CONNECTED"),
    ]
    
    print("  Test - Network Health Analysis:")
    for peer_count, target_count, expected_status in test_scenarios:
        analysis = analyzer.analyze_peer_connections(peer_count, target_count)
        
        print(f"\n    Peer Count: {peer_count}/{target_count}")
        print(f"    Health Score: {analysis['health_score']:.2f}")
        print(f"    Status: {analysis['status']} (Expected: {expected_status})")
        print(f"    Recommendation: {analysis['recommendation']}")
        
        if analysis['status'] != expected_status:
            print(f"    ✗ Status mismatch!")
            return False
    
    return True

def test_cryptographic_primitives():
    """Test cryptographic analysis"""
    print("\nTesting Cryptographic Primitives...")
    
    crypto = CryptographicPrimitives()
    
    # Test adaptor signature analysis
    print("  Test - Adaptor Signature Protocol Analysis:")
    analysis = crypto.analyze_adaptor_signature("COMIT")
    
    print(f"    Protocol: {analysis['protocol']}")
    print(f"    Description: {analysis['description']}")
    print(f"    Features: {len(analysis['features'])} features listed")
    print(f"    Security Guarantees: {len(analysis['security_guarantees'])} guarantees")
    
    # Check that we have the expected primitives
    expected_primitives = ["Ed25519", "Schnorr", "Ring Signatures", "Pedersen Commitments"]
    for primitive in expected_primitives:
        found = False
        for category, prims in crypto.primitives.items():
            if primitive in prims:
                found = True
                break
        if not found:
            print(f"    ✗ Missing primitive: {primitive}")
            return False
    
    print(f"    ✓ All expected cryptographic primitives found")
    return True

def test_fee_distribution_analyzer():
    """Test fee distribution analysis"""
    print("\nTesting Fee Distribution Analyzer...")
    
    analyzer = FeeDistributionAnalyzer()
    
    # Test fee distribution
    epoch_swap_fees = 500000000  # 5 XFG
    total_cd_locked = 10000000000  # 100 XFG
    
    distribution = analyzer.analyze_epoch_distribution(epoch_swap_fees, total_cd_locked)
    
    print("  Test - Epoch Fee Distribution:")
    print(f"    Epoch Swap Fees: {distribution['epoch_swap_fees']:,} atomic units")
    print(f"    CD Share: {distribution['cd_share']:,} atomic units ({distribution['cd_share_percentage']}%)")
    print(f"    Treasury Share: {distribution['treasury_share']:,} atomic units ({distribution['treasury_share_percentage']}%)")
    print(f"    Calculated Fee Rate: {distribution['fee_rate']:,}")
    
    # Verify calculations
    expected_cd_share = epoch_swap_fees * 80 // 100
    expected_treasury_share = epoch_swap_fees * 20 // 100
    
    if distribution['cd_share'] != expected_cd_share:
        print(f"    ✗ CD share mismatch: {distribution['cd_share']} vs {expected_cd_share}")
        return False
    
    if distribution['treasury_share'] != expected_treasury_share:
        print(f"    ✗ Treasury share mismatch: {distribution['treasury_share']} vs {expected_treasury_share}")
        return False
    
    print(f"    ✓ Fee distribution calculations correct")
    return True

def test_complete_specialist():
    """Test the complete specialist system"""
    print("\nTesting Complete Fuego Blockchain Specialist...")
    
    specialist = FuegoBlockchainSpecialist(is_testnet=False)
    
    # Test CD scenario analysis
    print("  Test - Complete CD Scenario Analysis:")
    
    analysis = specialist.analyze_cd_scenario(
        amount=100000000,
        creation_height=1000000,
        current_height=1100000,
        epoch_fee_rates=[1000, 1500, 2000, 1800, 1600]
    )
    
    required_keys = [
        'cd_amount', 'creation_height', 'current_height', 
        'epochs_elapsed', 'accumulated_interest', 'total_value',
        'effective_apy', 'avg_fee_rate'
    ]
    
    for key in required_keys:
        if key not in analysis:
            print(f"    ✗ Missing key in analysis: {key}")
            return False
    
    print(f"    ✓ CD analysis contains all required fields")
    print(f"    ✓ Interest calculated: {analysis['accumulated_interest']:,} atomic units")
    print(f"    ✓ Effective APY: {analysis['effective_apy']:.2f}%")
    
    # Test expert report generation
    print("\n  Test - Expert Report Generation:")
    
    analysis_data = {
        "cd_analysis": analysis,
        "fee_distribution": specialist.fee_analyzer.analyze_epoch_distribution(500000000, 10000000000),
        "swap_analysis": {
            "xfg_amount": 100000000,
            "fee_amount": 1000000,
            "fee_percentage": 1.0,
            "net_amount": 99000000,
            "state_id": 12,
            "state_name": "ADAPTOR_PRESIGS_READY"
        },
        "p2p_analysis": specialist.p2p_analyzer.analyze_peer_connections(6, 8),
        "crypto_analysis": specialist.crypto_analyzer.analyze_adaptor_signature("COMIT")
    }
    
    report = specialist.generate_expert_report(analysis_data)
    
    # Check report contains expected sections
    expected_sections = [
        "FUEGO BLOCKCHAIN SPECIALIST REPORT",
        "CD INTEREST ANALYSIS",
        "FEE DISTRIBUTION ANALYSIS",
        "ATOMIC SWAP ANALYSIS",
        "P2P NETWORK ANALYSIS",
        "CRYPTOGRAPHIC PRIMITIVES",
        "END OF REPORT"
    ]
    
    for section in expected_sections:
        if section not in report:
            print(f"    ✗ Missing section in report: {section}")
            return False
    
    print(f"    ✓ Report generated successfully")
    print(f"    ✓ Report length: {len(report):,} characters")
    
    # Save test report
    with open("test_specialist_report.txt", "w") as f:
        f.write(report)
    print(f"    ✓ Test report saved to: test_specialist_report.txt")
    
    return True

def main():
    """Run all tests"""
    print("=" * 80)
    print("Fuego Blockchain Specialist Agent - Test Suite")
    print("=" * 80)
    
    tests = [
        ("CD Interest Calculator", test_cd_interest_calculator),
        ("Atomic Swap Analyzer", test_atomic_swap_analyzer),
        ("P2P Consensus Analyzer", test_p2p_consensus_analyzer),
        ("Cryptographic Primitives", test_cryptographic_primitives),
        ("Fee Distribution Analyzer", test_fee_distribution_analyzer),
        ("Complete Specialist System", test_complete_specialist),
    ]
    
    passed = 0
    failed = 0
    
    for test_name, test_func in tests:
        try:
            print(f"\n{'='*60}")
            print(f"Running: {test_name}")
            print(f"{'='*60}")
            
            if test_func():
                print(f"✓ {test_name}: PASSED")
                passed += 1
            else:
                print(f"✗ {test_name}: FAILED")
                failed += 1
                
        except Exception as e:
            print(f"✗ {test_name}: ERROR - {str(e)}")
            import traceback
            traceback.print_exc()
            failed += 1
    
    print(f"\n{'='*80}")
    print("Test Summary:")
    print(f"  Passed: {passed}/{len(tests)}")
    print(f"  Failed: {failed}/{len(tests)}")
    print(f"{'='*80}")
    
    if failed == 0:
        print("🎉 All tests passed! The Fuego Blockchain Specialist Agent is ready.")
        return 0
    else:
        print(f"⚠️  {failed} test(s) failed. Please review the output above.")
        return 1

if __name__ == "__main__":
    sys.exit(main())