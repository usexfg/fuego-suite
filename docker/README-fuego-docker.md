# 🐳 Fuego Docker - Super Easy Setup Guide

<img title="The Long Night Is Coming" src="https://github.com/usexfg/fuego-data/blob/master/fuego-images/fuegoline.gif?raw=true" width="200">

**Fuego** is an open-source peer-to-peer decentralized private cryptocurrency built by advocates of freedom through sound money and free open-source software, based upon the CryptoNote protocol & philosophy.

This guide provides a **super easy setup** for running Fuego using Docker containers.

## 🚀 Quick Start (5 Minutes Setup)

### Prerequisites
- Docker installed ([Install Docker](https://docs.docker.com/get-docker/))
- Docker Compose installed ([Install Docker Compose](https://docs.docker.com/compose/install/))
- At least 4GB RAM and 10GB free disk space

### One-Command Setup
```bash
# Clone the repository
git clone https://github.com/usexfg/fuego
cd fuego

# Run the super easy setup script
chmod +x fuego-docker-setup.sh
./fuego-docker-setup.sh
```

That's it! Your Fuego node will be running in minutes. 🎉

## 📋 What Gets Installed

The setup script automatically creates:

- ✅ **Fuego Node** - Main blockchain daemon
- ✅ **Fuego Wallet** - Wallet service (optional)
- ✅ **Nginx Proxy** - Web interface (optional)
- ✅ **Monitoring Stack** - Prometheus + Grafana (optional)
- ✅ **All Configuration Files** - Ready to use
- ✅ **Data Directories** - Persistent storage

## 🎛️ Available Profiles

Choose what you want to run:

### Basic Node (Default)
```bash
./fuego-docker-setup.sh start node
```
- Fuego blockchain node only
- Ports: 20808 (P2P), 28180 (RPC)

### Node + Wallet
```bash
./fuego-docker-setup.sh start wallet
```
- Blockchain node + wallet service
- Ports: 20808, 28180, 8070 (Wallet)

### Full Web Interface
```bash
./fuego-docker-setup.sh start web
```
- Node + Wallet + Nginx web proxy
- Web interface at: http://localhost

### Monitoring Stack
```bash
./fuego-docker-setup.sh start monitoring
```
- Node + Wallet + Prometheus + Grafana
- Grafana dashboard at: http://localhost:3000

### Everything (Full Stack)
```bash
./fuego-docker-setup.sh start full
```
- All services including web interface and monitoring

## 🛠️ Management Commands

### Check Status
```bash
./fuego-docker-setup.sh status
```

### View Logs
```bash
# Node logs
./fuego-docker-setup.sh logs fuego-node

# Wallet logs
./fuego-docker-setup.sh logs fuego-wallet

# All logs
docker-compose -f docker-compose.fuego-docker.yml logs -f
```

### Stop Services
```bash
./fuego-docker-setup.sh stop
```

### Restart Services
```bash
./fuego-docker-setup.sh restart
```

### Clean Up (Remove Everything)
```bash
./fuego-docker-setup.sh clean
```

## 🌐 Web Interfaces

Once running, access these interfaces:

| Service | URL | Description |
|---------|-----|-------------|
| **Node RPC** | http://localhost:28180 | Direct node API |
| **Wallet RPC** | http://localhost:8070 | Wallet service API |
| **Web Interface** | http://localhost | Nginx proxy (if using web profile) |
| **Grafana** | http://localhost:3000 | Monitoring dashboard (if using monitoring profile) |
| **Prometheus** | http://localhost:9090 | Metrics (if using monitoring profile) |

## 📁 Directory Structure

```
fuego/
├── data/                    # Persistent data
│   ├── fuego/              # Blockchain data
│   ├── wallet/             # Wallet data
│   └── logs/               # Log files
├── config/                 # Configuration files
├── nginx/                  # Web proxy config
├── monitoring/             # Monitoring stack config
├── scripts/                # Utility scripts
├── Dockerfile.fuego-docker # Enhanced Dockerfile
├── docker-compose.fuego-docker.yml
├── fuego-docker-setup.sh   # Setup script
└── README-fuego-docker.md  # This file
```

## ⚙️ Configuration

### Environment Variables
Edit `.env` file to customize:

```bash
# Data paths
FUEGO_DATA_PATH=./data/fuego
FUEGO_WALLET_PATH=./data/wallet
FUEGO_LOGS_PATH=./data/logs

# Build settings
BUILD_TYPE=Release
ENABLE_OPTIMIZATIONS=ON
PARALLEL_BUILD=4

# Network ports
P2P_PORT=20808
RPC_PORT=28180
WALLET_PORT=8070

# Logging
FUEGO_LOG_LEVEL=2
```

### Custom Configuration
Create `config/fuego.conf` for custom node settings:

```ini
# Example custom configuration
rpc-bind-ip=0.0.0.0
rpc-bind-port=28180
p2p-bind-ip=0.0.0.0
p2p-bind-port=20808
log-level=2
data-dir=/home/fuego/.fuego
```

## 🔧 Advanced Usage

### Manual Docker Commands
```bash
# Build image manually
docker build -f Dockerfile.fuego-docker -t fuego:latest .

# Run node manually
docker run -d \
  --name fuego-node \
  -p 20808:20808 \
  -p 28180:28180 \
  -v $(pwd)/data/fuego:/home/fuego/.fuego \
  fuego:latest

# Run wallet manually
docker run -d \
  --name fuego-wallet \
  -p 8070:8070 \
  -v $(pwd)/data/wallet:/home/fuego/.fuego-wallet \
  fuego:latest \
  walletd \
  --daemon-host=fuego-node \
  --daemon-port=28180 \
  --bind-port=8070 \
  --bind-ip=0.0.0.0
```

### Development Mode
```bash
# Start development environment
./fuego-docker-setup.sh start development

# Access CLI container
docker exec -it fuego-cli bash
```

### Multi-Architecture Builds
```bash
# Build for multiple platforms
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -f Dockerfile.fuego-docker \
  -t fuego:multi-arch .
```

## 📊 Monitoring & Health Checks

### Built-in Health Checks
- Node health: `curl http://localhost:28180/getinfo`
- Wallet health: `curl http://localhost:8070/getinfo`

### Monitoring Stack (Optional)
When using the monitoring profile:
- **Prometheus**: Collects metrics from Fuego services
- **Grafana**: Visualizes metrics with dashboards
- **Default credentials**: admin/admin

### Custom Metrics
The monitoring stack tracks:
- Blockchain sync status
- Network connections
- Transaction processing
- Memory and CPU usage
- Disk I/O

## 🔒 Security Features

- ✅ **Non-root user**: Runs as `fuego` user (UID 1000)
- ✅ **Multi-stage build**: Minimal runtime image
- ✅ **Network isolation**: Custom Docker network
- ✅ **Volume encryption**: Optional encrypted volumes
- ✅ **Health checks**: Automatic service monitoring
- ✅ **Resource limits**: Configurable CPU/memory limits

## 🚨 Troubleshooting

### Common Issues

**Port Already in Use**
```bash
# Check what's using the port
sudo netstat -tulpn | grep :20808

# Use different ports in .env file
P2P_PORT=30808
RPC_PORT=38180
```

**Permission Issues**
```bash
# Fix volume permissions
sudo chown -R 1000:1000 ./data

# Or run setup as root
sudo ./fuego-docker-setup.sh
```

**Container Won't Start**
```bash
# Check logs
./fuego-docker-setup.sh logs fuego-node

# Run interactively for debugging
docker run -it --entrypoint bash fuego:latest
```

**Out of Disk Space**
```bash
# Clean up Docker
docker system prune -a

# Check disk usage
df -h
docker system df
```

### Performance Tuning

**Resource Limits**
```bash
# Add to docker-compose.fuego-docker.yml
services:
  fuego-node:
    deploy:
      resources:
        limits:
          cpus: '2.0'
          memory: 4G
        reservations:
          cpus: '1.0'
          memory: 2G
```

**Storage Optimization**
```bash
# Use faster storage
docker run --storage-opt size=100G fuego:latest

# Use SSD for data directory
ln -s /mnt/ssd/fuego-data ./data/fuego
```

## 📚 API Reference

### Node RPC Endpoints
```bash
# Get node info
curl http://localhost:28180/getinfo

# Get blockchain info
curl http://localhost:28180/getblockcount

# Get peer list
curl http://localhost:28180/getpeerlist
```

### Wallet RPC Endpoints
```bash
# Get wallet info
curl http://localhost:8070/getinfo

# Get balance
curl http://localhost:8070/getbalance

# Get addresses
curl http://localhost:8070/getaddresses
```

## 🤝 Contributing

To improve the Docker setup:

1. **Test changes locally**
2. **Update documentation**
3. **Submit pull request**

### Development Setup
```bash
# Fork and clone
git clone https://github.com/your-username/fuego
cd fuego

# Make changes
# Test with setup script
./fuego-docker-setup.sh

# Submit PR
```

## 📞 Support

- **Discord**: https://discord.gg/5UJcJJg
- **GitHub Issues**: https://github.com/usexfg/fuego/issues
- **Documentation**: https://github.com/usexfg/fuego

## 📄 License

This project is licensed under the GPL-3.0 License - see the [LICENSE](LICENSE) file for details.

---

**🔥 Happy Dockerizing! 🐳**

*Built with ❤️ by the Fuego community*