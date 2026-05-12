# Multi-stage Dockerfile for Fuego Cryptocurrency
# Stage 1: Build environment
FROM ubuntu:22.04 as builder

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /src

# Copy source code
COPY . .

# Build arguments for optimization
ARG BUILD_TYPE=Release
ARG ENABLE_OPTIMIZATIONS=ON

# Build Fuego with optimizations
RUN mkdir -p build && cd build && \
    cmake \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DBUILD_PERFORMANCE_TOOLS=${ENABLE_OPTIMIZATIONS} \
        -DBUILD_TESTS=OFF \
        -DSTATIC=ON \
        .. && \
    make -j$(nproc) && \
    strip src/fuegod src/walletd || true

# Stage 2: Runtime environment
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Create fuego user for security
RUN useradd -m -u 1000 -s /bin/bash fuego

# Copy binaries from builder stage
COPY --from=builder /src/build/src/fuegod /usr/local/bin/
COPY --from=builder /src/build/src/walletd /usr/local/bin/

# Set executable permissions
RUN chmod +x /usr/local/bin/fuegod /usr/local/bin/walletd

# Switch to fuego user
USER fuego
WORKDIR /home/fuego

# Create data directory
RUN mkdir -p /home/fuego/.fuego

# Create volume for blockchain data
VOLUME ["/home/fuego/.fuego"]

# Expose ports
# 20808 - P2P port
# 28180 - RPC port
EXPOSE 20808 28180

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:28180/getinfo || exit 1

# Default command
ENTRYPOINT ["fuegod"]
CMD ["--data-dir=/home/fuego/.fuego", "--rpc-bind-ip=0.0.0.0", "--rpc-bind-port=28180"]

# Labels for metadata
LABEL maintainer="Fuego Development Team"
LABEL description="Fuego Cryptocurrency Node"
LABEL version="latest"
LABEL org.opencontainers.image.source="https://github.com/usexfg/fuego"
LABEL org.opencontainers.image.documentation="https://github.com/usexfg/fuego"
LABEL org.opencontainers.image.licenses="GPL-3.0" 