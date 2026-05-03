from web3 import Web3
from eth_account import Account

RPC_URL = "https://ethereum-sepolia.publicnode.com"
PRIVATE_KEY = "3c61dd6641102a67c7190e62c4737264e967931bb708249f5ca3b0f14b74a3fa"
ACCOUNT = Account.from_key(PRIVATE_KEY)

# laeviathen style HTLC bytecode (simple version)
BYTECODE = "0x6080604052348015600f57600080fd5b506004361060285760003560e01c80630601db9081526004808357600b80600080fd5b80600080fd5b6004361060525760003560e01c8063e11e66018081526004808357600b80600080fd5b80600080fd5b6004361060745760003560e01c8063902786268081526004808357600b80600080fd5b80600080fd5b6004361060965760003560e01c8063e860a2718081526004808357600b80600080fd5b80600080fd5b6004361060ba5760003560e01c8063d20549688081526004808357600b80600080fd5b80600080fd5b6004361060cd5760003560e01c806332d07d678081526004808357600b80600080fd5b80600080fd5b6004361060ef5760003560e01c8063c65618f08081526004808357600b80600080fd5b80600080fd5b600080fd"

def deploy():
    w3 = Web3(Web3.HTTPProvider(RPC_URL))
    nonce = w3.eth.get_transaction_count(ACCOUNT.address)
    
    tx = {
        'from': ACCOUNT.address,
        'nonce': nonce,
        'gas': 3000000,
        'gasPrice': w3.eth.gas_price,
        'data': BYTECODE,
        'chainId': 11155111
    }
    
    signed_tx = w3.eth.account.sign_transaction(tx, PRIVATE_KEY)
    tx_hash = w3.eth.send_raw_transaction(signed_tx.raw_transaction)
    print(f"Transaction Hash: {tx_hash.hex()}")
    
    receipt = w3.eth.wait_for_transaction_receipt(tx_hash)
    print(f"Contract Address: {receipt.contractAddress}")

if __name__ == "__main__":
    deploy()
