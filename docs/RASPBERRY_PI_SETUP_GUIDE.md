# 🍓 Raspberry Pi Fuego Node Setup Guide

A complete guide to set up a Raspberry Pi from scratch to run a Fuego CLI node.

## 📋 Table of Contents
1. [Hardware Requirements](#hardware-requirements)
2. [Operating System Setup](#operating-system-setup)
3. [Initial Configuration](#initial-configuration)
4. [Dependencies Installation](#dependencies-installation)
5. [Building Fuego](#building-fuego)
6. [Running the Node](#running-the-node)
7. [Configuration](#configuration)
8. [Monitoring & Maintenance](#monitoring--maintenance)
9. [Troubleshooting](#troubleshooting)

---

## 🔧 Hardware Requirements

### Minimum Requirements
- **Raspberry Pi**: 4B (4GB RAM recommended) or 5
- **Storage**: 64GB+ microSD card (Class 10, UHS-I)
- **Power**: Official Raspberry Pi power supply (5V/3A)
- **Network**: Ethernet cable or WiFi connection
- **Cooling**: Passive heatsink or active cooling (recommended)

### Recommended Setup
- **Raspberry Pi**: 5 (8GB RAM)
- **Storage**: 128GB+ microSD card (Class 10, UHS-I)
- **Power**: Official Raspberry Pi 5 power supply (5V/5A)
- **Network**: Gigabit Ethernet connection
- **Cooling**: Active cooling with fan
- **Case**: Official Raspberry Pi case with cooling

---

## 🖥️ Operating System Setup

### 1. Download Raspberry Pi OS
```bash
# Download Raspberry Pi OS Lite (64-bit) from:
# https://www.raspberrypi.com/software/operating-systems/
# Choose: "Raspberry Pi OS Lite (64-bit)"
```

### 2. Flash the OS
```bash
# Using Raspberry Pi Imager (recommended)
# 1. Download from: https://www.raspberrypi.com/software/
# 2. Insert microSD card
# 3. Select OS: Raspberry Pi OS Lite (64-bit)
# 4. Select storage: Your microSD card
# 5. Click "Write"
```

### 3. Enable SSH (Optional)
```bash
# Create empty 'ssh' file in boot partition
# This enables SSH access without monitor/keyboard
touch /Volumes/boot/ssh
```

---

## ⚙️ Initial Configuration

### 1. First Boot
```bash
# Connect to your Pi via SSH or monitor
ssh pi@raspberrypi.local
# Default password: raspberry

# Or connect monitor/keyboard and login
```

### 2. Update System
```bash
# Update package lists
sudo apt update

# Upgrade all packages
sudo apt upgrade -y

# Reboot if kernel was updated
sudo reboot
```

### 3. Configure System
```bash
# Run configuration tool
sudo raspi-config

# Recommended settings:
# - System Options > Password: Change default password
# - System Options > Hostname: Set to "fuego-node"
# - Localization Options > Locale: Set your locale
# - Localization Options > Timezone: Set your timezone
# - Performance Options > GPU Memory: 16
# - Advanced Options > Expand Filesystem: Yes
```

### 4. Set Hostname
```bash
# Set hostname to fuego-node
sudo hostnamectl set-hostname fuego-node

# Update /etc/hosts
echo "127.0.1.1 fuego-node" | sudo tee -a /etc/hosts
```

---

## 📦 Dependencies Installation

### 1. Install Build Tools
```bash
# Install essential build tools
sudo apt install -y build-essential cmake git wget curl

# Install additional development tools
sudo apt install -y pkg-config libssl-dev libboost-all-dev
```

### 2. Install Boost 1.86 (if needed)
```bash
# Check current Boost version
dpkg -l | grep libboost

# If Boost < 1.86, install from source
cd /tmp
wget https://sourceforge.net/projects/boost/files/release/1.86.0/source/boost_1_86_0.tar.gz
tar -xjf boost_1_83_0.tar.bz2
cd boost_1_83_0

# Configure and build Boost
./bootstrap.sh
./b2 --prefix=/usr/local --with-system --with-filesystem --with-thread --with-date_time --with-chrono --with-regex --with-serialization --with-program_options --with-coroutine --with-context --with-atomic install

# Update library cache
sudo ldconfig
```

### 3. Install Additional Dependencies
```bash
# Install Python and other utilities
sudo apt install -y python3 python3-pip htop screen

# Install monitoring tools
sudo apt install -y nethogs iotop
```

---

## 🔨 Building Fuego

### 1. Clone Repository
```bash
# Create directory for Fuego
mkdir -p ~/fuego
cd ~/fuego

# Clone the repository
git clone https://github.com/usexfg/fuego.git
cd fuego
```

### 2. Create Build Directory
```bash
# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=/usr/local ..

# Build (this may take 1-2 hours on Pi)
make -j$(nproc)
```

### 3. Install Binaries
```bash
# Create installation directory
sudo mkdir -p /usr/local/bin/fuego

# Copy executables
sudo cp src/fuegod /usr/local/bin/fuego/
sudo cp src/fuego-wallet-cli /usr/local/bin/fuego/
sudo cp src/walletd /usr/local/bin/fuego/

# Make executable
sudo chmod +x /usr/local/bin/fuego/*

# Create symlinks
sudo ln -sf /usr/local/bin/fuego/fuegod /usr/local/bin/fuegod
sudo ln -sf /usr/local/bin/fuego/fuego-wallet-cli /usr/local/bin/fuego-wallet-cli
sudo ln -sf /usr/local/bin/fuego/walletd /usr/local/bin/walletd
```

---

## 🚀 Running the Node

### 1. Create Data Directory
```bash
# Create data directory
mkdir -p ~/.fuego

# Set permissions
chmod 700 ~/.fuego
```

### 2. Create Configuration File
```bash
# Create config file
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
```

### 3. Start the Daemon
```bash
# Start daemon in background
nohup fuegod --config-file ~/.fuego/fuego.conf > ~/.fuego/daemon.log 2>&1 &

# Check if it's running
ps aux | grep fuegod

# Check logs
tail -f ~/.fuego/daemon.log
```

### 4. Verify Node Status
```bash
# Check if daemon is responding
curl -X POST http://127.0.0.1:18180/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"test","method":"getblockcount","params":{}}'
```

---

## ⚙️ Configuration

### 1. Firewall Setup
```bash
# Install UFW if not present
sudo apt install -y ufw

# Configure firewall
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow ssh
sudo ufw allow 10808/tcp  # P2P port
sudo ufw allow 18180/tcp  # RPC port (if needed externally)

# Enable firewall
sudo ufw enable
```

### 2. Port Forwarding (Router)
```
Forward these ports to your Pi's IP:
- 10808 TCP/UDP (P2P)
- 18180 TCP (RPC, optional)
```

### 3. Systemd Service (Optional)
```bash
# Create systemd service file
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
sudo systemctl start fuego-daemon

# Check status
sudo systemctl status fuego-daemon
```

---

## 📊 Monitoring & Maintenance

### 1. Monitoring Scripts
```bash
# Create monitoring script
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

echo "=== Network Status ==="
curl -s -X POST http://127.0.0.1:18180/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"test","method":"getblockcount","params":{}}' \
  | jq -r '.result.count // "Error"'
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
```

### 2. Log Rotation
```bash
# Create logrotate configuration
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
```

### 3. Automatic Updates
```bash
# Create update script
cat > ~/update_fuego.sh << 'EOF'
#!/bin/bash
cd ~/fuego
git pull origin master
cd build
make -j$(nproc)
sudo cp src/fuegod /usr/local/bin/fuego/
sudo systemctl restart fuego-daemon
echo "Fuego updated and restarted"
EOF

chmod +x ~/update_fuego.sh
```

---

## 🔧 Troubleshooting

### Common Issues

#### 1. Build Failures
```bash
# Clear build cache
cd ~/fuego/build
make clean
rm -rf CMakeCache.txt CMakeFiles/

# Rebuild
cmake -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=/usr/local ..
make -j$(nproc)
```

#### 2. Memory Issues
```bash
# Increase swap space
sudo fallocate -l 2G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Make permanent
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

#### 3. Network Issues
```bash
# Check network connectivity
ping -c 4 8.8.8.8

# Check port status
sudo netstat -tlnp | grep :10808

# Check firewall
sudo ufw status
```

#### 4. Performance Issues
```bash
# Monitor system resources
htop

# Check disk I/O
iotop

# Check network usage
nethogs
```

### Log Analysis
```bash
# View daemon logs
tail -f ~/.fuego/daemon.log

# Search for errors
grep -i error ~/.fuego/daemon.log

# Check system logs
sudo journalctl -u fuego-daemon -f
```

---

## 📈 Performance Optimization

### 1. Overclocking (Optional)
```bash
# Edit config
sudo nano /boot/config.txt

# Add these lines (for Pi 4):
over_voltage=2
arm_freq=1750
gpu_freq=600

# Reboot
sudo reboot
```

### 2. SSD Setup (Advanced)
```bash
# If using USB SSD, ensure it's mounted properly
lsblk
sudo mount /dev/sda1 /mnt/ssd

# Move data directory to SSD
sudo systemctl stop fuego-daemon
mv ~/.fuego /mnt/ssd/
ln -s /mnt/ssd/.fuego ~/.fuego
sudo systemctl start fuego-daemon
```

---

## 🎯 Next Steps

1. **Monitor your node** using the monitoring script
2. **Set up alerts** for when the node goes down
3. **Configure backup** for your wallet files
4. **Join the community** on Discord/Telegram
5. **Consider running multiple nodes** for redundancy

---

## 📞 Support

- **GitHub Issues**: https://github.com/usexfg/fuego/issues
- **Documentation**: Check the main repository README
- **Community**: Join Fuego Discord/Telegram channels

---

**Happy mining! 🚀**

*This guide was created for Fuego v1.9.3.9063 and Raspberry Pi OS 64-bit.* 