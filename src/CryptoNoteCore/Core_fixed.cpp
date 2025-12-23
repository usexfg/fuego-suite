bool core::check_tx_fee(const Transaction& tx, size_t blobSize, uint8_t blockMajorVersion, tx_verification_context& tvc) {
	uint64_t inputs_amount = 0;
	if (!get_inputs_money_amount(tx, inputs_amount)) {
		tvc.m_verification_failed = true;
		return false;
	}

	uint64_t outputs_amount = get_outs_money_amount(tx);

	if (outputs_amount > inputs_amount) {
		logger(DEBUGGING) << "transaction uses more money then it has: use " << m_currency.formatAmount(outputs_amount) <<
			", have " << m_currency.formatAmount(inputs_amount);
		tvc.m_verification_failed = true;
		return false;
	}

	Crypto::Hash h = NULL_HASH;
	getObjectHash(tx, h, blobSize);
	const uint64_t fee = inputs_amount - outputs_amount;
	bool isFusionTransaction = fee == 0 && m_currency.isFusionTransaction(tx, blobSize);

	// Use flat minimum fee (no dynamic calculation)
	if (!isFusionTransaction && fee < m_currency.minimumFee(blockMajorVersion)) {
		logger(DEBUGGING) << "transaction fee is not enough: " << m_currency.formatAmount(fee) <<
			", minimum fee: " << m_currency.formatAmount(m_currency.minimumFee(blockMajorVersion));
		tvc.m_verification_failed = true;
		tvc.m_tx_fee_too_small = true;
		return false;
	}

	return true;
}