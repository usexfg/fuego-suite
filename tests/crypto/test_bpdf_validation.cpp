#include "src/CryptoNoteCore/BurnProofDataFileGenerator.h"
#include <iostream>
#include <fstream>

using namespace CryptoNote;

int main() {
    // Test BPDF validation
    std::cout << "Testing BPDF validation..." << std::endl;
    
    // Create a test BPDF file
    std::string testTxHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    Crypto::SecretKey testSecret;
    // Fill with test data
    for (int i = 0; i < 32; i++) {
        testSecret.data[i] = i;
    }
    std::string testRecipient = "0x1234567890123456789012345678901234567890";
    uint64_t testAmount = 8000000; // 0.8 XFG
    std::string testOutputPath = "test_bpdf.json";
    
    // Generate BPDF
    auto result = BurnProofDataFileGenerator::generateBPDF(
        testTxHash, testSecret, testRecipient, testAmount, testOutputPath);
    
    if (result) {
        std::cout << "Failed to generate BPDF: " << result.message() << std::endl;
        return 1;
    }
    
    std::cout << "BPDF generated successfully" << std::endl;
    
    // Test validation
    bool isValid = BurnProofDataFileGenerator::validateBPDF(testOutputPath);
    
    if (isValid) {
        std::cout << "BPDF validation PASSED" << std::endl;
    } else {
        std::cout << "BPDF validation FAILED" << std::endl;
    }
    
    // Clean up
    std::remove(testOutputPath.c_str());
    
    return 0;
}