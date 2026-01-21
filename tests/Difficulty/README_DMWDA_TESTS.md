# DMWDA Test Suite

## Dynamic Multi-Window Difficulty Algorithm Comprehensive Testing

This test suite provides comprehensive testing and analysis for Fuego's **Dynamic Multi-Window Difficulty Algorithm (DMWDA)**, ensuring the algorithm is hardened and ready for production deployment.

## 🎯 **Test Objectives**

- **Validate Algorithm Stability** across various network conditions
- **Test Emergency Response** to sudden hash rate changes
- **Verify Block Stealing Prevention** mechanisms
- **Measure Performance** under stress conditions
- **Ensure Cross-Platform Compatibility**

## 📁 **Test Suite Components**

### **1. Core Test Suite (`DMWDA_TestSuite.cpp`)**
C++ implementation testing the actual DMWDA algorithm with:
- **8 Comprehensive Test Scenarios**
- **Real Algorithm Integration**
- **Performance Measurements**
- **Edge Case Handling**

### **2. Test Runner (`scripts/run_dmwda_tests.sh`)**
Automated test execution with:
- **Build Configuration**
- **Test Execution**
- **Result Analysis**
- **Report Generation**

### **3. Advanced Analysis (`scripts/dmwda_analysis.py`)**
Python-based analysis providing:
- **Mathematical Simulation**
- **Visualization Generation**
- **Statistical Analysis**
- **Comprehensive Reporting**

### **4. Configuration (`tests/dmwda_test_config.json`)**
Test parameters and validation criteria

## 🧪 **Test Scenarios**

### **1. Normal Operation**
- **Description**: Steady hash rate operation
- **Blocks**: 200 blocks with normal timing (480s ± 10%)
- **Expected**: Stable difficulty adjustment
- **Validation**: Stability score > 8.0

### **2. Hash Rate Spike**
- **Description**: 10x hash rate increase
- **Phases**: Normal (50) → Spike (50) → Recovery (100)
- **Expected**: Emergency response, rapid difficulty increase
- **Validation**: Response time < 5 blocks

### **3. Hash Rate Drop**
- **Description**: 10x hash rate decrease
- **Phases**: Normal (50) → Drop (50) → Recovery (100)
- **Expected**: Emergency response, rapid difficulty decrease
- **Validation**: Response time < 5 blocks

### **4. Block Stealing Attempt**
- **Description**: Consecutive fast blocks
- **Phases**: Normal (50) → Stealing (5) → Recovery (100)
- **Expected**: Block stealing detection, emergency response
- **Validation**: 100% detection rate

### **5. Oscillating Hash Rate**
- **Description**: Alternating fast/slow blocks
- **Pattern**: 240s / 720s alternating
- **Expected**: Smoothing, reduced oscillations
- **Validation**: Stability score > 6.0

### **6. Gradual Hash Rate Change**
- **Description**: Smooth hash rate transition
- **Phases**: Increase (100) → Decrease (100)
- **Expected**: Smooth adaptation, no emergency mode
- **Validation**: Response time < 15 blocks

### **7. Edge Cases**
- **Description**: Boundary conditions
- **Tests**: Minimum blocks, extreme timing, limits
- **Expected**: Graceful handling
- **Validation**: No crashes, reasonable outputs

### **8. Stress Test**
- **Description**: Random extreme conditions
- **Blocks**: 1000 blocks with random timing (48s - 4800s)
- **Expected**: Algorithm stability
- **Validation**: Stability score > 5.0

## 🚀 **Running the Tests**

### **Quick Start**
```bash
# Run comprehensive test suite
./scripts/run_dmwda_tests.sh
```

### **Advanced Analysis**
```bash
# Run Python analysis (requires numpy, matplotlib)
python3 scripts/dmwda_analysis.py
```

### **Manual Testing**
```bash
# Build and run individual tests
mkdir build_test && cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
make DMWDA_TestSuite
./tests/DMWDA_TestSuite
```

## 📊 **Performance Metrics**

### **Stability Score (0-10)**
- **Calculation**: `10 - (block_time_cv + difficulty_cv) * 5`
- **Target**: > 7.0 for normal operation
- **Higher is better**

### **Response Time (blocks)**
- **Calculation**: Average blocks to detect and respond to changes
- **Target**: < 10 blocks for most scenarios
- **Lower is better**

### **Emergency Activations**
- **Calculation**: Count of blocks triggering emergency response
- **Target**: Appropriate for scenario (not too many, not too few)

### **Block Stealing Detection**
- **Calculation**: Count of detected block stealing patterns
- **Target**: 100% detection rate

## 🔍 **Validation Criteria**

### **Stability Requirements**
- **Normal Operation**: Stability score > 8.0
- **Hash Rate Changes**: Stability score > 6.0
- **Stress Test**: Stability score > 5.0

### **Response Requirements**
- **Emergency Scenarios**: Response time < 5 blocks
- **Gradual Changes**: Response time < 15 blocks
- **Oscillating**: Response time < 10 blocks

### **Safety Requirements**
- **Minimum Difficulty**: Never below 10,000
- **Maximum Difficulty**: Reasonable upper bound
- **Emergency Bounds**: 0.1x to 10x adjustment range

## 📈 **Expected Results**

### **Normal Operation**
```
Average block time: 8.00 minutes (target: 8.0 minutes)
Difficulty range: 800,000 - 1,200,000
Emergency activations: 0 blocks
Block stealing attempts: 0 blocks
Stability score: 9.2/10
```

### **Hash Rate Spike**
```
Average block time: 4.80 minutes (target: 8.0 minutes)
Difficulty range: 100,000 - 4,000,000
Emergency activations: 50 blocks
Block stealing attempts: 0 blocks
Stability score: 7.8/10
Response time: 3.2 blocks
```

### **Block Stealing Attempt**
```
Average block time: 7.20 minutes (target: 8.0 minutes)
Difficulty range: 500,000 - 2,000,000
Emergency activations: 5 blocks
Block stealing attempts: 5 blocks
Stability score: 8.5/10
Response time: 1.0 blocks
```

## 🛠️ **Troubleshooting**

### **Build Issues**
```bash
# Ensure all dependencies are installed
sudo apt-get install build-essential cmake
# or on macOS
brew install cmake
```

### **Python Analysis Issues**
```bash
# Install required packages
pip3 install numpy matplotlib pandas
```

### **Test Failures**
- Check that DMWDA algorithm is properly integrated
- Verify emergency response thresholds
- Ensure block stealing detection is active

## 📋 **Test Reports**

The test suite generates several reports:

- **`dmwda_test_results.txt`** - Full test output
- **`dmwda_test_report.md`** - Summary report
- **`dmwda_analysis_report.json`** - Detailed JSON data
- **`dmwda_analysis_report.md`** - Human-readable analysis
- **`dmwda_analysis.png`** - Visualization plots

## 🎯 **Success Criteria**

The DMWDA test suite is considered **PASSED** when:

✅ **All 8 test scenarios complete successfully**  
✅ **Stability scores meet validation criteria**  
✅ **Response times are within acceptable limits**  
✅ **Emergency response functions correctly**  
✅ **Block stealing prevention is active**  
✅ **No crashes or memory leaks**  
✅ **Cross-platform compatibility verified**  

## 🔧 **Configuration**

Test parameters can be modified in `tests/dmwda_test_config.json`:

- **Window sizes** (short: 15, medium: 45, long: 120)
- **Adjustment limits** (min: 0.5x, max: 4.0x)
- **Emergency thresholds** (0.1x to 10x)
- **Target block time** (480 seconds)

## 📚 **References**

- **ZAWY's LWMA-1**: Mathematical foundation
- **Fuego's AMWDA**: Enhanced implementation
- **Block Stealing Prevention**: Original innovation
- **Emergency Response**: Crisis management system

---

**DMWDA Test Suite**: Comprehensive testing for production-ready difficulty algorithm 🚀
