# 🔥 Fuego Testnet Docker Setup

This guide will help you set up a Fuego testnet using Docker for development and testing purposes.

## 🚀 Quick Start

### **Option 1: Automated Setup (Recommended)**
```bash
# Navigate to the docker directory
cd docker

# Run the automated setup script
./setup-testnet.sh
```

### **Option 2: Manual Setup**
```bash
# Navigate to the docker directory
cd docker

# Build and start the testnet
docker-compose -f docker-compose.testnet.yml up -d

# Check the status
docker ps --filter "name=fuego-testnet"
```

## 📋 **What's Included**

The testnet setup includes:

- **Fuego Node** (`fuego-testnet-node`) - Blockchain node with testnet mode
- **Wallet Service** (`fuego-testnet-wallet`) - Wallet RPC service
- **Nginx Proxy** (optional) - Web proxy for external access

## 🔧 **Configuration**

### **Testnet Features**
- ✅ **Testnet Mode** - Separate from mainnet
- ✅ **Dynamic Supply Tracking** - Full dynamic money supply functionality
- ✅ **FOREVER Deposits** - Burn deposit testing
- ✅ **RPC API** - Complete API access
- ✅ **Wallet Integration** - Full wallet functionality

### **Ports**
- `20808` - P2P networking port
- `28280` - Node RPC API port
- `8070` - Wallet service port

### **Volumes**
- `fuego-testnet-data` - Blockchain data
- `fuego-testnet-wallet-data` - Wallet data

## 🛠️ **Usage**

### **Start the Testnet**
```bash
# Start all services
docker-compose -f docker-compose.testnet.yml up -d

# Start only the node
docker-compose -f docker-compose.testnet.yml up fuego-testnet-node -d

# Start with web proxy
docker-compose -f docker-compose.testnet.yml --profile web up -d
```

### **Stop the Testnet**
```bash
# Stop all services
docker-compose -f docker-compose.testnet.yml down

# Stop and remove volumes (WARNING: This deletes all data)
docker-compose -f docker-compose.testnet.yml down -v
```

### **View Logs**
```bash
# View node logs
docker logs fuego-testnet-node

# View wallet logs
docker logs fuego-testnet-wallet

# Follow logs in real-time
docker logs -f fuego-testnet-node
```

### **Access the API**
```bash
# Get node info
curl -X POST http://localhost:28180/getinfo

# Get block count
curl -X POST http://localhost:28180/getblockcount

# Get dynamic supply overview
curl -X POST http://localhost:28180/getDynamicSupplyOverview

# Get wallet info
curl -X POST http://localhost:8070/getinfo
```

## 🧪 **Testing Dynamic Supply**

### **Test FOREVER Deposits**
```bash
# Create a FOREVER deposit (burn deposit)
curl -X POST http://localhost:8070/createDeposit \
  -H "Content-Type: application/json" \
  -d '{
    "amount": 800000000,
    "term": 4294967295,
    "sourceAddress": "your_address_here"
  }'
```

### **Check Supply Metrics**
```bash
# Get complete supply overview
curl -X POST http://localhost:28280/getDynamicSupplyOverview

# Get circulating supply
curl -X POST http://localhost:28280/getCirculatingSupply

# Get total burned XFG
curl -X POST http://localhost:28280/getTotalBurnedXfg
```

## 🔍 **Monitoring**

### **Health Checks**
```bash
# Check container health
docker ps --filter "name=fuego-testnet"

# Check specific service
docker inspect fuego-testnet-node --format='{{.State.Health.Status}}'
```

### **Resource Usage**
```bash
# Monitor resource usage
docker stats fuego-testnet-node fuego-testnet-wallet

# Check disk usage
docker system df
```

## 🐛 **Troubleshooting**

### **Common Issues**

**Container Won't Start**
```bash
# Check logs for errors
docker logs fuego-testnet-node

# Check if ports are available
netstat -tulpn | grep :28280
```

**Permission Issues**
```bash
# Fix volume permissions
sudo chown -R 1000:1000 /var/lib/docker/volumes/fuego-testnet-data/_data
```

**Build Failures**
```bash
# Clean and rebuild
docker-compose -f docker-compose.testnet.yml down
docker system prune -a
docker-compose -f docker-compose.testnet.yml build --no-cache
```

### **Reset Testnet**
```bash
# Complete reset (WARNING: Deletes all data)
docker-compose -f docker-compose.testnet.yml down -v
docker volume prune -f
./setup-testnet.sh
```

## 📊 **Development Workflow**

1. **Start Testnet** - Run `./setup-testnet.sh`
2. **Test Features** - Use RPC API to test dynamic supply
3. **Monitor Logs** - Watch for errors and performance
4. **Reset When Needed** - Clean slate for new tests
5. **Stop When Done** - Clean up resources

## 🔗 **Useful Commands**

```bash
# Quick status check
docker ps --filter "name=fuego-testnet"

# View all logs
docker-compose -f docker-compose.testnet.yml logs

# Restart services
docker-compose -f docker-compose.testnet.yml restart

# Update and rebuild
docker-compose -f docker-compose.testnet.yml pull
docker-compose -f docker-compose.testnet.yml build --no-cache
docker-compose -f docker-compose.testnet.yml up -d
```

## 📚 **Additional Resources**

- [Fuego Documentation](https://github.com/usexfg/fuego)
- [Docker Documentation](https://docs.docker.com/)
- [Dynamic Supply Documentation](./README.md)

---

**🔥 Happy Testing! 🐳**
