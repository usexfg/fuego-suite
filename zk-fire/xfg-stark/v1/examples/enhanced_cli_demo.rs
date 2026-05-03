use std::process::Command;
use std::path::Path;

fn main() {
    println!("🚀 Enhanced XFG STARK CLI Demo");
    println!("===============================");
    
    // Check if the enhanced CLI binary exists
    let cli_path = "target/debug/xfg-stark-enhanced-cli";
    if !Path::new(cli_path).exists() {
        println!("❌ Enhanced CLI binary not found. Please build it first:");
        println!("   cargo build --bin xfg-stark-enhanced-cli");
        return;
    }
    
    println!("✅ Enhanced CLI binary found");
    
    // Show help information
    println!("\n📖 Available Commands:");
    let help_output = Command::new(cli_path)
        .arg("--help")
        .output()
        .expect("Failed to execute CLI");
    
    println!("{}", String::from_utf8_lossy(&help_output.stdout));
    
    // Demonstrate the prove-and-verify command
    println!("\n🔧 Prove-and-Verify Command Demo:");
    let prove_help = Command::new(cli_path)
        .args(&["prove-and-verify", "--help"])
        .output()
        .expect("Failed to execute CLI");
    
    println!("{}", String::from_utf8_lossy(&help_output.stdout));
    
    println!("\n💡 Usage Example:");
    println!("   {} prove-and-verify \\", cli_path);
    println!("     --input burn-package.json \\");
    println!("     --output complete-proof.json \\");
    println!("     --eldernode-endpoint https://eldernodes.fuego.network/api/v1/verify");
    
    println!("\n🎯 Key Features:");
    println!("   • Parallel STARK generation and Eldernode verification");
    println!("   • Real-time progress tracking");
    println!("   • Identical inputs for both processes");
    println!("   • Input consistency verification");
    println!("   • Complete proof package output");
    
    println!("\n🔒 Security Benefits:");
    println!("   • Prevents data manipulation attacks");
    println!("   • Ensures input consistency between systems");
    println!("   • Optimized user experience with parallel processing");
    println!("   • Comprehensive verification workflow");
}
