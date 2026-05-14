# Fuego I2P Integration Guide

This guide explains how to configure Fuego to use the I2P network for enhanced privacy and censorship resistance.

## What is I2P?

I2P (Invisible Internet Project) is a decentralized, peer-to-peer network layer that provides strong privacy and anonymity. Unlike Tor, I2P is designed primarily for internal services and has no exit nodes to the regular internet.

**Benefits of using I2P with Fuego:**
- All P2P traffic stays within the I2P network (no clearnet exposure)
- Garlic routing provides better resistance to traffic analysis
- Built-in distributed hash table for peer discovery
- Designed for hidden services from the ground up

## Prerequisites

### Install i2pd (I2P Router)

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install i2pd
```

**Arch Linux:**
```bash
sudo pacman -S i2pd
```

**Fedora:**
```bash
sudo dnf install i2pd
```

**From source:**
```bash
git clone https://github.com/PurpleI2P/i2pd.git
cd i2pd && mkdir build && cd build
cmake .. -DWITH_UPNP=OFF -DWITH_HTTPS=OFF
make -j4
sudo make install
```

### Start i2pd

```bash
# Start i2pd as a service
sudo systemctl start i2pd
sudo systemctl enable i2pd  # Auto-start on boot

# Or run manually in background
i2pd --daemon
```

## Configuration

### 1. Configure i2pd

Edit `/etc/i2pd/i2pd.conf` (or `~/.i2pd/i2pd.conf`):

```ini
# Enable SOCKS5 proxy (required for Fuego)
socksport = 9150

# HTTP proxy (optional, for browsing I2P sites)
httpport = 4444

# SAM bridge (advanced)
samport = 7656

# I2P data directory (optional)
# i2pdir = /var/lib/i2pd

# Bandwidth limits (adjust for your connection)
# bandwidth = O
# Options: K (56KB/s), L (256KB/s), O (2048KB/s), P (8192KB/s), X (unlimited)
```

Restart i2pd after configuration changes:
```bash
sudo systemctl restart i2pd
```

### 2. Verify i2pd is Running

```bash
# Check if i2pd is running
curl -s http://localhost:7070/info | head

# Expected output should show router status
```

### 3. Configure Fuego

Create or edit `~/.fuego/fuego.conf`:

```ini
# Network privacy settings
p2p-use-i2p = true
i2p-socks-host = 127.0.0.1
i2p-socks-port = 9150

# Standard P2P settings
p2p-bind-port = 19994
allow-local-ip = false

# Optional: Add I2P seed nodes
seed-node = xfg.i2p
# seed-node = another-peer.i2p

# Logging
log-level = INFO
log-file = ~/.fuego/fuego.log
```

## Connecting I2P Peers

### Finding I2P Peers

I2P uses destinations (like `xfg123abc.i2p`) instead of IP addresses. You'll need to bootstrap connections somehow:

1. **Initial peer exchange via clearnet** (first connection only):
   ```bash
   # Add a trusted clearnet peer to bootstrap
   ./fuegod --add-peer 123.456.78.90:19994
   ```

2. **After first connection**, Fuego will discover I2P peers automatically.

3. **I2P address book** - Once connected to I2P peers, Fuego can use the distributed peer table.

### Sharing Your I2P Address

To share your node's I2P address for others to connect:

```bash
# Get your I2P destination (SAM API required)
curl -s "http://localhost:7656/samjs/destination.js?echo=1"
```

## Advanced Configuration

### FuegoTor Fallback

If I2P is unavailable, Fuego can fall back to Tor:

```ini
p2p-use-tor = true
tor-socks-host = 127.0.0.1
tor-socks-port = 9050

# Priority: I2P first, then Tor
privacy-network-priority = i2p,tor
```

### Multiple Privacy Networks

Run both Tor and I2P simultaneously:

```ini
# I2P configuration
p2p-use-i2p = true
i2p-socks-host = 127.0.0.1
i2p-socks-port = 9150

# Tor configuration
p2p-use-tor = true
tor-socks-host = 127.0.0.1
tor-socks-port = 9050
```

## Troubleshooting

### "Connection refused" errors

1. Verify i2pd is running:
   ```bash
   systemctl status i2pd
   ```

2. Check SOCKS5 proxy is listening:
   ```bash
   ss -tlnp | grep 9150
   ```

3. Test SOCKS5 connection:
   ```bash
   curl --socks5 127.0.0.1:9150 http://check.i2p
   ```

### "I2P peer not found"

- I2P takes time to build tunnels. Wait 5-10 minutes after starting i2pd.
- Check i2pd logs: `journalctl -u i2pd -f`
- Verify your I2P subscription is working

### Slow connections

- I2P is inherently slower than clearnet due to routing.
- Increase i2pd bandwidth settings in i2pd.conf
- Restart i2pd after changing settings.

## Security Notes

1. **Always use strong anonymity settings in i2pd** for maximum privacy
2. **Don't mix I2P and clearnet identities** if privacy is critical
3. **Keep i2pd updated** for security patches
4. **Verify your I2P tunnel** is working correctly before transacting

## Command Reference

```bash
# Start fuegod with I2P
./fuegod --p2p-use-i2p --i2p-socks-host 127.0.0.1 --i2p-socks-port 9150

# Check node status
./fuegod --status

# View connected peers (including I2P)
./fuegod --get_peer_list | grep "\.i2p"

# Force peer exchange
./fuegod --refresh_peer_list
```

## Further Reading

- [I2P Project](https://geti2p.net/)
- [i2pd GitHub](https://github.com/PurpleI2P/i2pd)
- [Fuego Wiki](https://github.com/usexfg/fuego-suite/wiki)
- [I2P Router Configuration](https://i2pd.readthedocs.io/en/latest/user-guide/configuration/)

## Support

- GitHub Issues: https://github.com/usexfg/fuego-suite/issues
- I2P Forums: https://i2pforum.net/
