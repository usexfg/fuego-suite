#!/bin/bash

# 🍓 Raspberry Pi Fuego Node Setup Script
# This script automates the setup of a Fuego node on Raspberry Pi

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

warn() {
    echo -e "${YELLOW}[$(date +'%Y-%m-%d %H:%M:%S')] WARNING: $1${NC}"
}

error() {
    echo -e "${RED}[$(date +'%Y-%m-%d %H:%M:%S')] ERROR: $1${NC}"
    exit 1
}

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   error "This script should not be run as root. Please run as pi user."
fi

# Check if running on Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    warn "This script is designed for Raspberry Pi. Continue anyway? (y/N)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

log "Starting Fuego Node Setup on Raspberry Pi..."

# Update system
log "Updating system packages..."
sudo apt update
sudo apt upgrade -y

# Install dependencies
log "Installing build dependencies..."
sudo apt install -y build-essential cmake git wget curl pkg-config libssl-dev

# Check Boost version
log "Checking Boost version..."
BOOST_VERSION=$(dpkg -l | grep libboost-dev | awk '{print $3}' | cut -d'.' -f1-2)
if [[ -z "$BOOST_VERSION" ]]; then
    log "Installing Boost from package manager..."
    sudo apt install -y libboost-all-dev
    BOOST_VERSION=$(dpkg -l | grep libboost-dev | awk '{print $3}' | cut -d'.' -f1-2)
fi

log "Boost version: $BOOST_VERSION"

# Install Boost 1.83 if needed
if [[ "$BOOST_VERSION" < "1.83" ]]; then
    log "Installing Boost 1.83 from source..."
    cd /tmp
    wget https://sourceforge.net/projects/boost/files/boost/1.83.0/boost_1_83_0.tar.bz2
    tar -xjf boost_1_83_0.tar.bz2
    cd boost_1_83_0
    
    ./bootstrap.sh
    ./b2 --prefix=/usr/local --with-system --with-filesystem --with-thread --with-date_time --with-chrono --with-regex --with-serialization --with-program_options install
    
    sudo ldconfig
    log "Boost 1.83 installed successfully"
else
    log "Boost version $BOOST_VERSION is sufficient"
fi

# Install additional tools
log "Installing monitoring tools..."
sudo apt install -y htop screen nethogs iotop python3 python3-pip

# Create fuego directory
log "Setting up Fuego directory..."
mkdir -p ~/fuego
cd ~/fuego

# Clone repository
if [[ ! -d "fuego" ]]; then
    log "Cloning Fuego repository..."
    git clone https://github.com/usexfg/fuego.git
else
    log "Fuego repository already exists, updating..."
    cd fuego
    git pull origin master
    cd ..
fi

cd fuego

# Create build directory
log "Setting up build environment..."
mkdir -p build
cd build

# Configure CMake
log "Configuring CMake..."
cmake -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=/usr/local ..

# Build Fuego
log "Building Fuego (this may take 1-2 hours)..."
make -j$(nproc)

log "Build completed successfully!"

# Install binaries
log "Installing binaries..."
sudo mkdir -p /usr/local/bin/fuego

sudo cp src/fuegod /usr/local/bin/fuego/
sudo cp src/fuego-wallet-cli /usr/local/bin/fuego/
sudo cp src/walletd /usr/local/bin/fuego/

sudo chmod +x /usr/local/bin/fuego/*

# Create symlinks
sudo ln -sf /usr/local/bin/fuego/fuegod /usr/local/bin/fuegod
sudo ln -sf /usr/local/bin/fuego/fuego-wallet-cli /usr/local/bin/fuego-wallet-cli
sudo ln -sf /usr/local/bin/fuego/walletd /usr/local/bin/walletd

# Create data directory
log "Setting up data directory..."
mkdir -p ~/.fuego
chmod 700 ~/.fuego

# Create configuration file
log "Creating configuration file..."
cat > ~/.fuego/fuego.conf << 'EOF'
# Fuego Daemon Configuration
data-dir=/home/pi/.fuego
log-file=/home/pi/.fuego/fuego.log
log-level=2

# Network Configuration
p2p-bind-ip=0.0.0.0
p2p-bind-port=10808
p2p-external-port=10808

# RPC Configuration
rpc-bind-ip=127.0.0.1
rpc-bind-port=18180

# Performance
enable-blockchain-indexes=1
enable-autosave=1

# Security
restricted-rpc=1
no-console=1
EOF

# Setup firewall
log "Setting up firewall..."
sudo apt install -y ufw
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow ssh
sudo ufw allow 10808/tcp
sudo ufw allow 18180/tcp
sudo ufw --force enable

# Create systemd service
log "Creating systemd service..."
sudo tee /etc/systemd/system/fuego-daemon.service > /dev/null << 'EOF'
[Unit]
Description=Fuego Daemon
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/.fuego
ExecStart=/usr/local/bin/fuegod --config-file /home/pi/.fuego/fuego.conf
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable fuego-daemon

# Create monitoring script
log "Creating monitoring script..."
cat > ~/monitor_fuego.sh << 'EOF'
#!/bin/bash
echo "=== Fuego Node Status ==="
echo "Date: $(date)"
echo "Uptime: $(uptime)"
echo ""

echo "=== Daemon Status ==="
if pgrep -x "fuegod" > /dev/null; then
    echo "✅ Daemon is running"
    echo "PID: $(pgrep fuegod)"
else
    echo "❌ Daemon is not running"
fi
echo ""

echo "=== System Resources ==="
echo "CPU: $(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)%"
echo "Memory: $(free -h | awk 'NR==2{printf "%.1f%%", $3*100/$2}')"
echo "Disk: $(df -h / | awk 'NR==2{print $5}')"
echo ""

echo "=== Network Connections ==="
netstat -an | grep :10808 | wc -l | xargs echo "P2P connections:"
EOF

chmod +x ~/monitor_fuego.sh

# Create update script
log "Creating update script..."
cat > ~/update_fuego.sh << 'EOF'
#!/bin/bash
cd ~/fuego/fuego
git pull origin master
cd build
make -j$(nproc)
sudo cp src/fuegod /usr/local/bin/fuego/
sudo systemctl restart fuego-daemon
echo "Fuego updated and restarted"
EOF

chmod +x ~/update_fuego.sh

# Setup log rotation
log "Setting up log rotation..."
sudo tee /etc/logrotate.d/fuego > /dev/null << 'EOF'
/home/pi/.fuego/*.log {
    daily
    missingok
    rotate 7
    compress
    delaycompress
    notifempty
    create 644 pi pi
    postrotate
        systemctl reload fuego-daemon 2>/dev/null || true
    endscript
}
EOF

# Start the daemon
log "Starting Fuego daemon..."
sudo systemctl start fuego-daemon

# Wait a moment for daemon to start
sleep 5

# Check if daemon is running
if sudo systemctl is-active --quiet fuego-daemon; then
    log "✅ Fuego daemon started successfully!"
else
    error "Failed to start Fuego daemon"
fi

# Display final information
echo ""
echo -e "${GREEN}🎉 Fuego Node Setup Complete! 🎉${NC}"
echo ""
echo -e "${BLUE}Useful Commands:${NC}"
echo "  Monitor node:     ~/monitor_fuego.sh"
echo "  Update Fuego:     ~/update_fuego.sh"
echo "  View logs:        tail -f ~/.fuego/daemon.log"
echo "  Service status:   sudo systemctl status fuego-daemon"
echo "  Stop service:     sudo systemctl stop fuego-daemon"
echo "  Start service:    sudo systemctl start fuego-daemon"
echo ""
echo -e "${BLUE}Important Information:${NC}"
echo "  - Data directory: ~/.fuego"
echo "  - Config file:    ~/.fuego/fuego.conf"
echo "  - P2P port:       10808"
echo "  - RPC port:       18180"
echo ""
echo -e "${YELLOW}Don't forget to:${NC}"
echo "  1. Forward port 10808 on your router"
echo "  2. Set up regular monitoring"
echo "  3. Keep your system updated"
echo ""
echo -e "${GREEN}Happy mining! 🚀${NC}" 