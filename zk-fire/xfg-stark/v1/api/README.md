# COLD Deposits API

API server for COLD deposit proof submission with domain linking and Fuego daemon integration.

## Features

- **Domain Linking**: EIP-712 signature binding claims to usexfg.org
- **Timestamp Validation**: Browser timestamp verification (±5 minutes)
- **Fuego Integration**: Queries trusted Fuego daemon for deposit data
- **Nullifier Protection**: Prevents double-claiming via nullifier tracking
- **Legacy Support**: 80% APY for 800 XFG deposits before 2026
- **Rate Limiting**: 10 requests per minute per IP
- **Health Checks**: Monitor Fuego daemon and Arbitrum connectivity

## Architecture

```
Browser (usexfg.org)
    ↓ EIP-712 signed request
API Server (this code)
    ↓ Query deposit data
Fuego Daemon (trusted node)
    ↓ Fetch on-chain data
API Server validates & submits
    ↓ claimCD() call
COLDDepositProofVerifier (Arbitrum)
    ↓ L2→L1 message
FuegoCOLDAOToken (Ethereum)
    ↓ Mint CD tokens
User receives CD
```

## Installation

```bash
cd api
npm install
```

## Configuration

Copy `.env.example` to `.env` and configure:

```bash
cp .env.example .env
nano .env
```

Required environment variables:
- `API_VERIFIER_PRIVATE_KEY`: Private key for submitting proofs
- `FUEGO_MAINNET_RPC`: Fuego mainnet daemon URL
- `FUEGO_TESTNET_RPC`: Fuego testnet daemon URL
- `ARB_SEPOLIA_RPC`: Arbitrum Sepolia RPC URL
- `COLD_VERIFIER_ADDRESS`: COLDDepositProofVerifier contract address

## Development

```bash
npm run dev
```

Server runs on http://localhost:3000

## Production

```bash
npm run build
npm start
```

## API Endpoints

### POST /api/cold/claim

Submit COLD deposit claim with domain binding.

**Request:**
```json
{
  "domainBinding": {
    "domain": "usexfg.org",
    "recipient": "0x...",
    "depositTxHash": "0x...",
    "timestamp": 1234567890000,
    "nonce": "0x..."
  },
  "signature": "0x..."
}
```

**Response (Success):**
```json
{
  "success": true,
  "txHash": "0x...",
  "tier": 2,
  "isLegacy": false,
  "estimatedCDAmount": "2640000000",
  "arbiscanUrl": "https://sepolia.arbiscan.io/tx/0x...",
  "message": "Proof submitted successfully. CD tokens will be minted on L1 in ~10 minutes."
}
```

**Response (Error):**
```json
{
  "success": false,
  "error": "Deposit not found on Fuego blockchain"
}
```

### GET /api/cold/health

Health check endpoint.

**Response:**
```json
{
  "success": true,
  "fuego": {
    "mainnet": "healthy",
    "testnet": "healthy"
  },
  "arbitrum": {
    "blockNumber": 12345678,
    "status": "healthy"
  },
  "apiVerifier": "0x..."
}
```

## Security

### Domain Binding

All claim requests must be signed with EIP-712 typed data:

```typescript
const signature = await wallet.signTypedData(
  {
    name: "COLD Deposits",
    version: "1",
    chainId: 421614
  },
  {
    DomainBinding: [
      { name: "domain", type: "string" },
      { name: "recipient", type: "address" },
      { name: "depositTxHash", type: "bytes32" },
      { name: "timestamp", type: "uint256" },
      { name: "nonce", type: "bytes32" }
    ]
  },
  domainBindingMessage
);
```

### Timestamp Validation

- Browser timestamp must be within ±5 minutes of server time
- Prevents replay attacks with stale signatures
- Validates deposit lock period not expired

### Nonce Tracking

- Each nonce can only be used once
- Prevents signature replay attacks
- Stored in-memory (use Redis in production)

### Rate Limiting

- 10 requests per minute per IP address
- Prevents spam and DoS attacks
- Configurable in `src/index.ts`

## Testing

```bash
# Run tests
npm test

# Test health endpoint
curl http://localhost:3000/api/cold/health

# Test claim (requires valid signature)
curl -X POST http://localhost:3000/api/cold/claim \
  -H "Content-Type: application/json" \
  -d @claim-request.json
```

## Deployment

### Docker

```bash
docker build -t cold-api .
docker run -p 3000:3000 --env-file .env cold-api
```

### PM2

```bash
npm install -g pm2
pm2 start dist/index.js --name cold-api
pm2 save
pm2 startup
```

## Monitoring

- Logs: `morgan` middleware logs all requests
- Errors: Console error logging
- Health: `/api/cold/health` endpoint
- Metrics: Integrate with Prometheus/Grafana

## Fuego Daemon Integration

The API queries a trusted Fuego daemon for deposit data:

### Expected RPC Methods:

**get_cold_deposit:**
```json
{
  "jsonrpc": "2.0",
  "method": "get_cold_deposit",
  "params": { "tx_hash": "0x..." },
  "id": 1
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "tx_hash": "0x...",
    "sender": "fuego_address",
    "amount": 8000000,
    "lock_period_months": 3,
    "timestamp": 1735689600,
    "nullifier": "0x...",
    "commitment": "0x...",
    "status": "locked"
  },
  "id": 1
}
```

**is_nullifier_used:**
```json
{
  "jsonrpc": "2.0",
  "method": "is_nullifier_used",
  "params": { "nullifier": "0x..." },
  "id": 1
}
```

## License

MIT

---

**Winter is coming. ❄️**
