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

### 1. Configure i2pd SOCKS5 Proxy

Edit `/etc/i2pd/i2pd.conf` (or `~/.i2pd/i2pd.conf`):

```ini
# Enable SOCKS5 proxy (required for Fuego outbound connections)
socksproxyenabled = true
socksport = 4447

# HTTP proxy (optional, for browsing I2P sites)
httpport = 4444

# SAM bridge (for inbound I2P connections - future feature)
samport = 7656

# Bandwidth limits (adjust for your connection)
# Options: K (56KB/s), L (256KB/s), O (2048KB/s), P (8192KB/s), X (unlimited)
bandwidth = O
```

Restart i2pd after configuration changes:
```bash
sudo systemctl restart i2pd
```

### 2. Verify i2pd is Running

```bash
# Check if i2pd is running
curl -s http://localhost:7070/info | head

# Check SOCKS5 proxy is listening
ss -tlnp | grep 4447
```

### 3. Configure Fuego

Start fuegod with I2P routing:

```bash
# Basic I2P outbound routing
./fuegod --p2p-use-i2p --i2p-socks-host 127.0.0.1 --i2p-socks-port 4447

# With Tor fallback
./fuegod --p2p-use-i2p --i2p-socks-port 4447 --p2p-use-tor --tor-socks-port 9050

# Clearnet isolation mode (I2P/Tor ONLY, no clearnet exposure)
./fuegod --p2p-use-i2p --i2p-socks-port 4447 --p2p-restrict-to-privacy-net

# With custom port and seed nodes
./fuegod --p2p-use-i2p --i2p-socks-port 4447 --p2p-bind-port 19994 \
  --seed-node 123.456.78.90:19994 --seed-node 98.76.54.32:19994
```

### CLI Flags Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--p2p-use-i2p` | `false` | Route outbound P2P connections through I2P SOCKS5 proxy |
| `--i2p-socks-host` | `127.0.0.1` | I2P SOCKS5 proxy host |
| `--i2p-socks-port` | `4447` | I2P SOCKS5 proxy port (i2pd default) |
| `--p2p-use-tor` | `false` | Route outbound P2P connections through Tor SOCKS5 proxy |
| `--tor-socks-host` | `127.0.0.1` | Tor SOCKS5 proxy host |
| `--tor-socks-port` | `9050` | Tor SOCKS5 proxy port |
| `--p2p-restrict-to-privacy-net` | `false` | Disable clearnet P2P listener; use only I2P/Tor |
| `--p2p-bind-port` | `10808` | P2P listening port (disabled when restrict-to-privacy-net) |
| `--seed-node` | (none) | Manual seed nodes (IP:port); needed when DNS is disabled |

## Connecting I2P Peers

### Finding Peers

When I2P is enabled, Fuego routes all outbound P2P connections through the i2pd SOCKS5 proxy. The proxy transparently handles I2P network routing.

**Important**: When `--p2p-use-i2p` is active:
- DNS seed resolution is automatically disabled (prevents DNS leaks)
- UPnP port mapping is automatically disabled (prevents real IP exposure)
- You **must** provide manual seed nodes via `--seed-node` to bootstrap

### Bootstrapping

```bash
# Option 1: Add known I2P-side peers (reachable through the I2P SOCKS5 proxy)
./fuegod --p2p-use-i2p --seed-node 10.11.12.13:10808 --seed-node 10.14.15.16:10808

# Option 2: Bootstrap via clearnet once, then switch to I2P-only
# First run (clearnet, builds peerlist):
./fuegod --add-peer 123.456.78.90:10808
# Second run (I2P, uses cached peerlist):
./fuegod --p2p-use-i2p --p2p-restrict-to-privacy-net
```

### I2P Port Reference

| Service | Default Port |
|---------|-------------|
| i2pd SOCKS5 proxy | **4447** |
| i2pd HTTP proxy | 4444 |
| i2pd SAM bridge | 7656 |
| i2pd Web console | 7070 |
| Java I2P SOCKS5 | 4447 |
| Tor SOCKS5 (system) | 9050 |
| Tor Browser SOCKS5 | 9150 |

**Note**: The SOCKS5 port for i2pd is **4447**, not 9150 (which is Tor Browser's SOCKS port).

## Privacy Hardening

### What Fuego does automatically

- **UPnP disabled**: When I2P or Tor is active, UPnP port mapping is skipped (prevents real IP leak to local router)
- **DNS seeds skipped**: DNS-based seed resolution is disabled (prevents DNS leak)
- **SOCKS5 for all P2P**: All outbound connections go through the SOCKS5 proxy, not direct TCP

### Clearnet Isolation Mode

`--p2p-restrict-to-privacy-net` provides maximum privacy:
- Clearnet TCP listener is **not bound** (no clearnet peers can connect to you)
- All P2P communication goes through I2P/Tor exclusively
- Your real IP address is never exposed to the Fuego network

**Recommended for privacy-critical users.**

### What Fuego does NOT yet do (future)

- **Zone-separated peerlists**: I2P and clearnet peers are not strictly separated
- **I2P inbound connections**: SAM bridge for receiving I2P connections is not yet implemented
- **I2P destination addresses**: `.b32.i2p` addresses in the peerlist format

## Troubleshooting

### "Connection refused" errors

1. Verify i2pd is running:
   ```bash
   systemctl status i2pd
   ```

2. Check SOCKS5 proxy is listening:
   ```bash
   ss -tlnp | grep 4447
   ```

3. Test SOCKS5 connection:
   ```bash
   curl --socks5 127.0.0.1:4447 http://check.i2p
   ```

### "No peers to connect to"

- DNS seeds are disabled when I2P is active. Provide `--seed-node` addresses.
- I2P takes time to build tunnels. Wait 5-10 minutes after starting i2pd.
- Check i2pd logs: `journalctl -u i2pd -f`

### Slow connections

- I2P is inherently slower than clearnet due to garlic routing.
- Increase i2pd bandwidth settings in i2pd.conf
- Restart i2pd after changing settings.

### Verification Checklist

Before trusting I2P for privacy:

- [ ] i2pd is running and has established tunnels (check web console)
- [ ] Fuego is started with `--p2p-use-i2p`
- [ ] UPnP is disabled (check Fuego logs: "Privacy network active — skipping UPnP")
- [ ] DNS seeds are skipped (check Fuego logs: "skipping DNS seed resolution")
- [ ] For maximum privacy: use `--p2p-restrict-to-privacy-net`
- [ ] Seed nodes are provided via `--seed-node`
- [ ] Check logs show "Privacy network enabled: I2P via proxy 127.0.0.1:4447"

## Security Notes

1. Keep i2pd updated for security patches
2. Use `--p2p-restrict-to-privacy-net` if you want zero clearnet exposure
3. I2P outbound only (inbound SAM support is planned for a future release)
4. Peerlist is shared between I2P and clearnet sessions; for strict isolation, use separate data directories
5. Transactions relayed through I2P still use the same RingCT privacy model

## Further Reading

- [I2P Project](https://geti2p.net/)
- [i2pd GitHub](https://github.com/PurpleI2P/i2pd)
- [Fuego Wiki](https://github.com/usexfg/fuego-suite/wiki)
- [I2P Router Configuration](https://i2pd.readthedocs.io/en/latest/user-guide/configuration/)
- [Monero I2P/Tor Integration](https://github.com/monero-project/monero) (reference architecture)

## Support

- GitHub Issues: https://github.com/usexfg/fuego-suite/issues
- I2P Forums: https://i2pforum.net/
