<img title="The Long Night Is Coming" src="https://github.com/usexfg/fuego-data/blob/master/fuego-images/fuegoline.gif?raw=true"><img/>
### Fuego is open-source peer-to-peer decentralized private cryptocurrency built by advocates of freedom thru sound money and free open-source software .

Based upon the CryptoNote protocol & philosophy.

#### Resources

-   [Website](https://usexfg.org)
-   Explorer: <http://fuego.spaceportx.net>
-   Explorer: <https://explore-xfg.loudmining.com>
-   Explorer: [http://radioactive.sytes.net](http://radioactive.sytes.net:8000/index.html)
-   [Discord](https://discord.gg/5UJcJJg)
-   [Twitter](https://twitter.com/useXFG)
-   [Medium](https://medium.com/@usexfg)
-   [Bitcoin Talk](https://bitcointalk.org/index.php?topic=2712001)

 ______________________________
 

<sup>"Working software is the primary measure of progress." [‣]</sup>

##### Master Status   
[![Build check](https://github.com/usexfg/fuego/actions/workflows/check.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/check.yml)

[![macOS](https://github.com/usexfg/fuego/actions/workflows/macOS.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/macOS.yml)

[![AppImage Linux](https://github.com/usexfg/fuego/actions/workflows/appimage.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/appimage.yml)

[![Ubuntu 24.04](https://github.com/usexfg/fuego/actions/workflows/ubuntu24.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/ubuntu24.yml)

[![Ubuntu 22.04](https://github.com/usexfg/fuego/actions/workflows/ubuntu22.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/ubuntu22.yml)

[![Windows](https://github.com/usexfg/fuego/actions/workflows/windows.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/windows.yml)

[![Docker Images](https://github.com/usexfg/fuego/actions/workflows/docker.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/docker.yml)

[![Android (Termux)](https://github.com/usexfg/fuego/actions/workflows/termux.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/termux.yml)

[![Raspberry Pi (ARM64)](https://github.com/usexfg/fuego/actions/workflows/raspberry-pi.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/raspberry-pi.yml)

[‣]:http://agilemanifesto.org/

#### Building On *nix

1. Dependencies: GCC 4.7.3 or later, CMake 2.8.6 or later, and Boost 1.55.

You may download them from:

* http://gcc.gnu.org/
* http://www.cmake.org/
* http://www.boost.org/


*** Alternatively, it may be possible to install them using a package manager by
executing the following command.
 ```
 sudo apt-get install build-essential git cmake libboost-all-dev libjsoncpp-dev libssl-dev
```

2. Clone Fuego repository
```
git clone https://github.com/usexfg/fuego

```
3. Open folder with copied repository
```
cd fuego
```
4. Building (Compiling)
    (resulting programs will be found in build/release/src)

```
make
```

Note: This will also build the TUI (Terminal User Interface) if Go is installed.

5. Starting Fuego daemon
```
cd fuego/build/release/src `
./fuegod
````
try --help from within dæmon for a full list of available commands
or <code>./fuegod --help</code> when outside of dæmon

### Terminal User Interface (TUI)

Fuego includes a Go-based Terminal User Interface for easy management of nodes and wallets. The TUI provides full support for Elderfier Staking and Burn2Mint flows.

#### Building the TUI

If you have Go 1.24+ installed, the TUI will be built automatically when running `make`. You can also build it separately:

```bash
make build-tui
```

#### Running the TUI

```bash
./tui/build/fuego-tui
```

Navigate with arrow keys or j/k, select with Enter, quit with q or Ctrl+C.

#### TUI Features

- Start/Stop Node
- Start Wallet RPC
- Create Wallet
- Get Balance
- Send Transaction
- Elderfier Menu (staking and voting)
- Burn2Mint Menu (XFG→HEAT conversion)

For detailed documentation, see `tui/README.md`.
_________________________________________________________
For the most user-friendly graphic interface experience

see [Fuego Desktop Wallet](https://github.com/usexfg/fuego-wallet). 
_________________________________________________________

_________________________________________________________
**Advanced options:**

* Parallel build: run `make -j<number of threads>` instead of `make`.
* Debug build: run `make build-debug`.
* Test suite: run `make test-release` to run tests in addition to building. Running `make test-debug` will do the same to the debug version.
* Building with Clang: it may be possible to use Clang instead of GCC, but this may not work everywhere. To build, run `export CC=clang CXX=clang++` before running `make`.

**************************************************************************************************
### Windows 10

#### Prerequisites

- Install [Visual Studio 2019 Community Edition](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=16)
- Install [CMake](https://cmake.org/download/)
- When installing Visual Studio, you need to install **Desktop development with C++** and the **MSVC v142 - VS 2019 C++ x64/x86 build tools** components. The option to install the v142 build tools can be found by expanding the "Desktop development with C++" node on the right. You will need this for the project to build correctly.
- Install [Boost 1.73.0](https://sourceforge.net/projects/boost/files/boost-binaries/1.73.0/boost_1_73_0-msvc-14.2-64.exe/download), **ensuring** you download the installer for **MSVC 14.2**.

#### Building

From the start menu, open 'x64 Native Tools Command Prompt for vs2019' or run "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsMSBuildCmd.bat" from any command prompt.

```bash
git clone https://github.com/usexfg/fuego
cd fuego
mkdir build
cmake .. -G "Visual Studio 16 2019" -A x64 -DBOOST_LIBRARYDIR="c:\local\boost_1_73_0\lib64-msvc-14.2"
msbuild fuegoX.sln /p:Configuration=Release /m
```

If the build is successful the binaries will be in the `src/Release` folder.

### macOS

#### Prerequisites

In order to install prerequisites, [XCode](https://developer.apple.com/xcode/) and [Homebrew](https://brew.sh/) needs to be installed.
Once both are ready, open Terminal app and run the following command to install additional tools:

```bash
$ xcode-select --install
```

On newer macOS versions (v10.14 and higher) this step is done through Software Update in System Preferences.

After that, proceed with installing dependencies:

```bash
$ brew install git python cmake gcc boost
```

#### Building

When all dependencies are installed, build Fuego core binaries:

```bash
$ git clone https://github.com/usexfg/fuego
$ cd fuego
$ mkdir build && cd $_
$ cmake ..
$ make
```

If the build is successful the binaries will be located in `src` directory.
*******************************

Join our ever-expanding community of Fuego holders thru [Discord](https://discordapp.com/invite/5UJcJJg), [Reddit](https://reddit.com/r/Fango), or [Twitter](https://twitter.com/usexfg).



