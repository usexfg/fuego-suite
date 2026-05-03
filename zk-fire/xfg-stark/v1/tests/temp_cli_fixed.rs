    // Validate block height is after XFG burn implementation (800,000+)
    if package.burn_transaction.block_height < 800_000 {
        println!("   ❌ Block height {} is before XFG burn implementation (800,000)", package.burn_transaction.block_height);
        return Err(XfgStarkError::ParseError("Block height must be after XFG burn implementation (800,000+)".to_string()));
    }

    // Validate network ID format
    if package.burn_transaction.network_id.is_empty() {
        println!("   ❌ Network ID is required");
        return Err(XfgStarkError::ParseError("Network ID cannot be empty".to_string()));
    }
