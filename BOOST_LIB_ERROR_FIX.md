# Fix: testnetd/daemon Missing Boost Library Error

## Error
```
./testnetd: error while loading shared libraries: libboost_filesystem.so.1.83.0: 
cannot open shared object file: No such file or directory
```

## Why This Happens
Pre-built binaries from CI are compiled against **Boost 1.83.0** (Ubuntu 24.04), but your system has **1.74.0** (Ubuntu 22.04/Debian 11) or **1.71.0** (Ubuntu 20.04).

---

## Quick Check
```bash
ldd testnetd | grep boost
# Shows: libboost_filesystem.so.1.83.0 => not found

cat /etc/os-release
# Shows your Ubuntu/Debian version
```

---

## Solutions

### ✅ Option 1: Build From Source (Recommended)
This creates a binary compatible with **your** system:

```bash
# Install ALL dependencies (including JSON support)
sudo apt update
sudo apt install -y build-essential git cmake libboost-all-dev libjsoncpp-dev libssl-dev

# Build Fuego
cd fuego
make -j$(nproc)

# Your compatible binary
./build/release/src/testnetd
```

### ⚠️ Option 2: Docker (Zero Dependency)
No builds, no library issues - just run:
```bash
docker run -d --name fuego-testnet -p 20808:20808 -p 28280:28280 -v fuego-data:/app/data fuegonetwork/testnetd:latest
```

### 🔗 Option 3: Symlink Hack (Quick Fix)
If you have Boost 1.74.0 but need 1.83.0:
```bash
sudo ln -s /usr/lib/x86_64-linux-gnu/libboost_filesystem.so.1.74.0 \
           /usr/lib/x86_64-linux-gnu/libboost_filesystem.so.1.83.0
sudo ln -s /usr/lib/x86_64-linux-gnu/libboost_program_options.so.1.74.0 \
           /usr/lib/x86_64-linux-gnu/libboost_program_options.so.1.83.0
sudo ldconfig
```
⚠️ **Only works if you have libboost 1.74.0 installed!**

---

## Build Requirements

Your system must have these **exact** packages:
```bash
sudo apt install build-essential git cmake libboost-all-dev libjsoncpp-dev libssl-dev
```

### Boost Versions by OS
| OS | Boost Version | Status |
|-----|---------------|--------|
| Ubuntu 20.04 | 1.71.0 | ❌ Rebuild needed |
| Ubuntu 22.04 | 1.74.0 | ❌ Rebuild needed |
| Ubuntu 24.04 | 1.83.0 | ✅ Works with CI binary |

---

## Prevent This Issue

### For Distributors
Build **static libraries** to eliminate dependency issues:
```bash
cmake -DBOOST_USE_STATIC=ON -DCMAKE_BUILD_TYPE=Release .
make -j$(nproc)
```

### For Users
**Always build from source** unless you have Ubuntu 24.04 or Docker.

---

## Need Help?
- `ldd ./testnetd` - shows all missing libraries
- `dpkg -l | grep libboost` - check installed Boost
- `cmake --version` - verify CMake is installed
- Docker is always the easiest solution: `doc
KUMENTARZ: KONIEC LINII
