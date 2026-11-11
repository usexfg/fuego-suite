#!/bin/bash

# Test Raspberry Pi ARM64 Cross-Compilation Build
# This script tests the Raspberry Pi build process locally

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🍓 Testing Raspberry Pi ARM64 Cross-Compilation Build${NC}"
echo "=================================================="

# Check if we're on Ubuntu/Debian
if ! command -v apt-get &> /dev/null; then
    echo -e "${RED}❌ This script requires Ubuntu/Debian with apt-get${NC}"
    exit 1
fi

# Install cross-compilation tools
echo -e "${YELLOW}📦 Installing ARM64 cross-compilation tools...${NC}"
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Install dependencies
echo -e "${YELLOW}📦 Installing build dependencies...${NC}"
sudo apt-get install -y cmake build-essential ninja-build pkg-config \
     libssl-dev libminiupnpc-dev libqrencode-dev libudev-dev \
     libunwind-dev liblzma-dev qtbase5-dev qtbase5-dev-tools \
     libicu-dev

# Create toolchain file
echo -e "${YELLOW}🔧 Creating ARM64 toolchain file...${NC}"
mkdir -p cmake
cat > cmake/arm64-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

# Download and build Boost for ARM64
echo -e "${YELLOW}📦 Building Boost for ARM64...${NC}"
BOOST_VERSION=1.83.0
BOOST_VERSION_UNDERSCORE=${BOOST_VERSION//./_}
BOOST_URL="https://sourceforge.net/projects/boost/files/boost/${BOOST_VERSION}/boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2/download"

if [ ! -f "boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2" ]; then
    wget --retry-connrefused --waitretry=5 --timeout=30 --tries=5 -O boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2 "$BOOST_URL"
fi

if [ ! -d "/tmp/boost-arm64" ]; then
    tar -xjf boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2
    cd boost_${BOOST_VERSION_UNDERSCORE}
    echo "using gcc : arm : aarch64-linux-gnu-g++ ;" > user-config.jam
    ./bootstrap.sh
    ./b2 --user-config=user-config.jam toolset=gcc-arm target-os=linux architecture=arm address-model=64 --prefix=/tmp/boost-arm64 install
    cd ..
fi

# Patch Boost
echo -e "${YELLOW}🔧 Patching Boost...${NC}"
BOOST_HEADER="boost/context/posix/stack_traits.hpp"
if [ -f "$BOOST_HEADER" ] && grep -q '#if defined(PTHREAD_STACK_MIN) && (PTHREAD_STACK_MIN > 0)' "$BOOST_HEADER"; then
    sed -i 's/#if defined(PTHREAD_STACK_MIN) && (PTHREAD_STACK_MIN > 0)/#if defined(PTHREAD_STACK_MIN)/' "$BOOST_HEADER"
fi

# Download and build ICU for ARM64
echo -e "${YELLOW}📦 Building ICU for ARM64...${NC}"
ICU_VERSION=76.1
ICU_URL="https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION//./-}/icu4c-${ICU_VERSION//./_}-src.tgz"

if [ ! -d "/tmp/icu-arm64" ]; then
    if [ ! -f "icu4c-${ICU_VERSION//./_}-src.tgz" ]; then
        wget --retry-connrefused --waitretry=5 --timeout=30 --tries=5 -O icu4c-${ICU_VERSION//./_}-src.tgz "$ICU_URL"
    fi
    tar -xzf icu4c-${ICU_VERSION//./_}-src.tgz
    cd icu/source
    export CC=aarch64-linux-gnu-gcc
    export CXX=aarch64-linux-gnu-g++
    export AR=aarch64-linux-gnu-ar
    export RANLIB=aarch64-linux-gnu-ranlib
    export STRIP=aarch64-linux-gnu-strip
    ./configure --host=aarch64-linux-gnu --prefix=/tmp/icu-arm64 --with-cross-build=/usr
    make -j$(nproc)
    make install
    cd ../..
fi

# Test build
echo -e "${YELLOW}🔨 Testing ARM64 build...${NC}"
build_folder="build-test/"
mkdir -p "$build_folder"
cd "$build_folder"

# Configure with CMake
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm64-toolchain.cmake \
      -DBOOST_ROOT=/tmp/boost-arm64 \
      -DICU_ROOT=/tmp/icu-arm64 \
      -DICU_INCLUDE_DIR=/tmp/icu-arm64/include \
      -DICU_DATA_LIBRARY=/tmp/icu-arm64/lib/libicudata.so \
      -DICU_I18N_LIBRARY=/tmp/icu-arm64/lib/libicui18n.so \
      -DICU_UC_LIBRARY=/tmp/icu-arm64/lib/libicuuc.so \
      -DCMAKE_PREFIX_PATH="/tmp/icu-arm64" \
      -DCMAKE_BUILD_TYPE=Release \
      -G Ninja ..

# Build with Ninja
echo -e "${YELLOW}🔨 Building with Ninja...${NC}"
ninja -j$(nproc)

# Check if executables were created
echo -e "${YELLOW}🔍 Checking built executables...${NC}"
exeFiles=()
for f in src/*; do 
  if [[ -x $f && -f $f ]]; then
    echo -e "${GREEN}✅ Found executable: $f${NC}"
    exeFiles+=( "$f" )
  fi
done

if [ ${#exeFiles[@]} -eq 0 ]; then
    echo -e "${RED}❌ No executables found in src/ directory${NC}"
    exit 1
fi

# Test file type
echo -e "${YELLOW}🔍 Checking file types...${NC}"
for f in "${exeFiles[@]}"; do
    file_type=$(file "$f")
    echo -e "${BLUE}📄 $f: $file_type${NC}"
    if [[ $file_type == *"aarch64"* ]]; then
        echo -e "${GREEN}✅ Correctly built for ARM64${NC}"
    else
        echo -e "${RED}❌ Not built for ARM64${NC}"
    fi
done

echo -e "${GREEN}🎉 Raspberry Pi ARM64 build test completed successfully!${NC}"
echo -e "${BLUE}📊 Built ${#exeFiles[@]} executables for ARM64${NC}"