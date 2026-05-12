# 🐳 Fuego Docker Setup

This directory contains Docker configuration files for running Fuego cryptocurrency nodes and services.

## 🚀 Quick Start

### **Option 1: Using Pre-built Images**
```bash
# Pull the latest image
docker pull ghcr.io/usexfg/fuego:latest

# Run a Fuego node
docker run -d \
  --name fuego-node \
  -p 20808:20808 \
  -p 28180:28180 \
  -v fuego-data:/home/fuego/.fuego \
  ghcr.io/usexfg/fuego:latest
```

### **Option 2: Building Locally**
```bash
# Build the image
docker build -t fuego:local .

# Run with Docker Compose
docker-compose up -d
```

## 📋 **Available Images**

| Registry | Image | Description |
|----------|-------|-------------|
| **GitHub Container Registry** | `ghcr.io/usexfg/fuego:latest` | Latest stable build |
| **GitHub Container Registry** | `ghcr.io/usexfg/fuego:v1.x.x` | Specific version |
| **Docker Hub** | `usexfg/fuego:latest` | Mirror on Docker Hub |

## 🔧 **Configuration**

### **Environment Variables**
- `FUEGO_LOG_LEVEL` - Log verbosity (0-4, default: 2)
- `BUILD_TYPE` - Build configuration (Release/Debug)
- `ENABLE_OPTIMIZATIONS` - Enable performance optimizations (ON/OFF)

### **Ports**
- `20808` - P2P networking port
- `28180` - RPC API port
- `8070` - Wallet service port (walletd)

### **Volumes**
- `/home/fuego/.fuego` - Blockchain data directory
- `/home/fuego/.fuego-wallet` - Wallet data directory

## 🏗️ **Docker Compose Services**

### **Basic Setup**
```bash
# Start node only
docker-compose up fuego-node

# Start node + wallet service
docker-compose up fuego-node fuego-wallet

# Start everything including web proxy
docker-compose --profile web up
```

### **Service Descriptions**
- **fuego-node** - Main blockchain node
- **fuego-wallet** - Wallet service daemon
- **nginx** - Web proxy (optional, use `--profile web`)

## 🔒 **Security Features**

- ✅ **Non-root user** - Runs as `fuego` user (UID 1000)
- ✅ **Multi-stage build** - Minimal runtime image
- ✅ **Health checks** - Automatic service monitoring
- ✅ **Network isolation** - Custom Docker network
- ✅ **Volume encryption** - Optional encrypted volumes

## 📊 **Monitoring**

### **Health Checks**
```bash
# Check container health
docker ps
docker inspect fuego-node --format='{{.State.Health.Status}}'

# View logs
docker logs fuego-node
docker logs fuego-wallet
```

### **Resource Usage**
```bash
# Monitor resource usage
docker stats fuego-node fuego-wallet

# Check disk usage
docker system df
```

## 🛠️ **Advanced Usage**

### **Custom Build Arguments**
```bash
# Build with debug symbols
docker build \
  --build-arg BUILD_TYPE=Debug \
  --build-arg ENABLE_OPTIMIZATIONS=OFF \
  -t fuego:debug .

# Build with maximum optimizations
docker build \
  --build-arg BUILD_TYPE=Release \
  --build-arg ENABLE_OPTIMIZATIONS=ON \
  -t fuego:optimized .
```

### **Multi-Architecture Builds**
```bash
# Build for multiple architectures
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --tag fuego:multi-arch .
```

### **Development Mode**
```bash
# Mount source code for development
docker run -it \
  -v $(pwd):/src \
  -w /src \
  ubuntu:22.04 \
  bash
```

## 🔧 **Troubleshooting**

### **Common Issues**

**Port Already in Use**
```bash
# Check what's using the port
sudo netstat -tulpn | grep :20808
sudo lsof -i :20808

# Use different ports
docker run -p 30808:20808 -p 38180:28180 ghcr.io/usexfg/fuego:latest
```

**Permission Issues**
```bash
# Fix volume permissions
sudo chown -R 1000:1000 /var/lib/docker/volumes/fuego-data/_data
```

**Container Won't Start**
```bash
# Check logs
docker logs fuego-node

# Run interactively for debugging
docker run -it --entrypoint bash ghcr.io/usexfg/fuego:latest
```

### **Performance Tuning**

**Resource Limits**
```bash
# Limit CPU and memory usage
docker run \
  --cpus="2.0" \
  --memory="4g" \
  --memory-swap="6g" \
  ghcr.io/usexfg/fuego:latest
```

**Storage Optimization**
```bash
# Use faster storage driver
docker run --storage-opt size=100G ghcr.io/usexfg/fuego:latest

# Clean up unused data
docker system prune -a
```

## 📚 **Additional Resources**

- [Fuego Documentation](https://github.com/usexfg/fuego)
- [Docker Best Practices](https://docs.docker.com/develop/best-practices/)
- [Docker Compose Reference](https://docs.docker.com/compose/)

## 🤝 **Contributing**

To improve the Docker setup:
1. Test changes locally
2. Update documentation
3. Submit pull request

---

**🔥 Happy Dockerizing! 🐳** 