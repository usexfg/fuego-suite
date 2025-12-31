# Docker Distribution for Fuego

## Overview
Docker provides a **zero-dependency** solution that eliminates the Boost library error on all systems.

## Quick Start

### Run testnetd
```bash
docker run -d \
  --name fuego-testnet \
  -p 20808:20808 \
  -p 28280:28280 \
  -v fuego-data:/app/data \
  fuego/testnetd:latest
```

### Run daemon
```bash
docker run -d \
  --name fuego-daemon \
  -p 10808:10808 \
  -p 18180:18180 \
  -v fuego-data:/app/data \
  fuego/daemon:latest
```

### View logs
```bash
docker logs -f fuego-testnet
```

---

## Docker Compose (Recommended)

Create `docker-compose.yml`:
```yaml
version: '3.8'

services:
  testnetd:
    image: fuego/testnetd:latest
    container_name: fuego-testnet
    restart: unless-stopped
    ports:
      - "20808:20808"  # P2P
      - "28280:28280"  # RPC
    volumes:
      - ./data:/app/data
      - ./config:/app/config
    environment:
      - LOG_LEVEL=2
    command: --config-file=config/testnet.conf --log-level=2
```

Run:
```bash
docker-compose up -d
docker-compose logs -f
```

---

## Building From Source

### Build the image
```bash
cd fuego
docker build -t fuego/testnetd:latest -f Dockerfiles/testnetd .
```

### Run and test
```bash
docker run --rm -it \
  -p 20808:20808 -p 28280:28280 \
  fuego/testnetd:latest --help
```

### Build for mainnet
```bash
docker build -t fuego/daemon:latest -f Dockerfiles/daemon .
```

---

## Dockerfiles

### Dockerfiles/testnetd
```dockerfile
FROM ubuntu:22.04 AS builder

# Install build dependencies (src/CryptoNoteConfig.h uses 20808/28280 for testnet)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libjsoncpp-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Build
RUN cmake -DCMAKE_BUILD_TYPE=Release . && \
    make -j$(nproc) testnetd

# Runtime stage - minimal dependencies!
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libboost-filesystem1.74.0 \
    libboost-program-options1.74.0 \
    libboost-system1.74.0 \
    libjsoncpp25 \
    libssl1.1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary
COPY --from=builder /build/build/src/testnetd ./
COPY --from=builder /build/src/config/ ./

# Create data directory
RUN mkdir -p /app/data

# TESTNET ports: 20808 P2P, 28280 RPC
EXPOSE 20808 28280

# Non-root user
RUN useradd -m -u 1000 -s /bin/bash fuego && \
    chown -R fuego:fuego /app
USER fuego

ENTRYPOINT ["./testnetd", "--data-dir=/app/data"]
CMD ["--log-level=1"]
```

### Dockerfiles/daemon
```dockerfile
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libjsoncpp-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Build mainnet daemon
RUN cmake -DCMAKE_BUILD_TYPE=Release . && \
    make -j$(nproc) daemon

# Runtime stage
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libboost-filesystem1.74.0 \
    libboost-program-options1.74.0 \
    libboost-system1.74.0 \
    libjsoncpp25 \
    libssl1.1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary
COPY --from=builder /build/build/src/daemon ./
COPY --from=builder /build/src/config/ ./

# Create data directory
RUN mkdir -p /app/data

# MAINNET ports: 10808 P2P, 18180 RPC
EXPOSE 10808 18180

# Non-root user
RUN useradd -m -u 1000 -s /bin/bash fuego && \
    chown -R fuego:fuego /app
USER fuego

ENTRYPOINT ["./daemon", "--data-dir=/app/data"]
CMD ["--log-level=2"]
```

---

## Configuration

### Mount config files
```bash
docker run -d \
  -v ./my-config.conf:/app/config/testnet.conf \
  -v ./my-genesis.txt:/app/config/genesis.txt \
  fuego/testnetd:latest --config-file=/app/config/testnet.conf
```

### Environment variables
```dockerfile
ENV LOG_LEVEL=2
ENV DATA_DIR=/app/data
```

Override at runtime:
```bash
docker run -e LOG_LEVEL=3 fuego/testnetd:latest
```

---

## Volume Management

### Persistent data
```bash
# Named volume
docker volume create fuego-data
docker run -v fuego-data:/app/data fuego/testnetd:latest

# Bind mount
docker run -v /host/path/to/data:/app/data fuego/testnetd:latest
```

### Backup wallet
```bash
docker cp fuego-testnet:/app/data/wallet.keys ./backup-keys
```

### Recover from backup
```bash
docker cp ./backup-keys fuego-testnet:/app/data/wallet.keys
```

---

## Troubleshooting

### Library errors in build
```bash
# Clean build
docker system prune -a
docker build --no-cache -t fuego/testnetd:latest -f Dockerfiles/testnetd .
```

### Container exits immediately
```bash
# Check logs
docker logs fuego-testnet

# Run interactively to debug
docker run -it --rm fuego/testnetd:latest --help
```

### Port conflicts
```bash
# Check if ports are in use
netstat -tuln | grep 28080

# Use different ports
-p 30808:20808 -p 38280:28280 fuego/testnetd:latest
```

---

## Multi-arch Builds (ARM64, AMD64)

```bash
# Enable buildx
docker buildx create --use

# Build for multiple architectures
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t fuego/testnetd:latest \
  -f Dockerfiles/testnetd \
  --push .
```

---

## CI/CD Integration

### GitHub Actions
```yaml
name: Build Docker

on:
  push:
    branches: [main]

jobs:
  docker:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Build
        run: |
          docker build -t fuego/testnetd:latest -f Dockerfiles/testnetd .
          
      - name: Push
        run: |
          echo ${{ secrets.DOCKER_PASSWORD }} | docker login -u ${{ secrets.DOCKER_USER }} --password-stdin
          docker tag fuego/testnetd:latest yourorg/fuego-testnetd:latest
          docker push yourorg/fuego-testnetd:latest
```

---

## Security Best Practices

### Non-root user
The Dockerfile already creates and runs as `fuego` user (UID 1000).

### Read-only filesystem
```bash
docker run --read-only -v fuego-data:/app/data fuego/testnetd:latest
```

### Resource limits
```bash
docker run \
  --memory=2g \
  --cpus=2 \
  fuego/testnetd:latest
```

---

## Current Status

- [x] Base Dockerfiles created
- [ ] Push to Docker Hub
- [ ] Create docker-compose.yml for users
- [ ] CI/CD automation
- [ ] ARM64 support testing

Next: Run `docker build -t fuego/testnetd:latest -f Dockerfiles/testnetd .` and test.
