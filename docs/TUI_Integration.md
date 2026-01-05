fuego-fresh/
├── tui/
│   ├── main.go           # TUI application entry point
│   ├── go.mod            # Go module definition
│   ├── go.sum            # Dependency checksums
│   ├── CMakeLists.txt    # CMake integration for builds
│   ├── README.md         # Detailed TUI documentation
│   ├── build/            # Built executable directory
│   └── src/              # Additional Go source files (if any)
├── scripts/
│   ├── install-go-deps.sh # Go installation script
│   └── build-tui.sh      # TUI-specific build script
└── CMakeLists.txt        # Main build system (updated)
```

## Build Process

### Prerequisites

- **CMake 3.16+** for the build system
- **Go 1.20+** for the TUI component (automatic detection)
- **GCC 4.7.3+** or **Clang** for the C++ components
- **Boost 1.86++** for the C++ components

### Building with CMake

The TUI is automatically included in the build process when Go is detected:

```bash
mkdir -p build/release
cd build/release
cmake ../..
make
```

The TUI executable will be created at: `build/release/tui/build/fuego-tui`

### Building with Make

The Makefile has been updated to include TUI compilation:

```bash
# Build all components including TUI
make

# Build TUI specifically
make build-tui
```

### Manual TUI Build

For TUI-specific development:

```bash
# Use the provided script
./scripts/build-tui.sh

# Or build directly with Go
cd tui
go mod tidy
go build -o build/fuego-tui
```

## Installation

### Binary Installation

The TUI is installed as part of the standard installation process:

```bash
make install
```

This will install `fuego-tui` to the standard binary directory (usually `/usr/local/bin`).

### Documentation Installation

TUI documentation is installed to: `/usr/local/share/doc/fuego/tui/README.md`

## Usage

Starting the TUI:

```bash
# If installed system-wide
fuego-tui

# If running from build directory
./build/release/tui/build/fuego-tui
```

### Key Features

1. **Node Management**
   - Start/Stop Fuego daemon
   - Check node status
   - View logs

2. **Wallet Operations**
   - Start Wallet RPC
   - Create new wallets
   - Check balance
   - Send transactions

3. **Elderfier Staking**
   - Complete Elderfyre Stayking process
   - Register to ENindex
   - View consensus requests
   - Submit votes

4. **Burn2Mint Flow**
   - Burn XFG on Fuego
   - Request Elderfier consensus
   - Generate STARK proofs
   - Submit to Arbitrum L2

## Security Considerations

### Threat Model

The TUI is designed to protect against:

1. **Network Observers**
   - RPC communications are encrypted
   - Request patterns are obfuscated when possible

2. **Local System Compromise**
   - No persistent storage of sensitive data
   - Memory is cleared after operations complete

3. **Metadata Leakage**
   - Minimal observable network traffic
   - Request timing randomization

### Best Practices

1. **Node Communication**
   - Always use locally running RPC endpoints
   - Verify certificate fingerprints for remote connections

2. **Wallet Operations**
   - Use a dedicated wallet file for the TUI
   - Consider using a hardware wallet for maximum security

3. **System Security**
   - Run the TUI on a secure system
   - Keep the terminal secure from observation

## Development

### Dependencies

The TUI uses the following Go libraries:
- `github.com/charmbracelet/bubbletea` - TUI framework
- `github.com/charmbracelet/lipgloss` - Styling

### Testing

TUI tests can be run with:

```bash
# Enable TUI tests in CMake
cmake -DBUILD_TUI_TESTS=ON ../..
make test_tui

# Or run directly with Go
cd tui
go test
```

### Integration Points

The TUI interfaces with the C++ components via these RPC endpoints:

- `localhost:18081` - Node RPC
- `localhost:18082` - Wallet RPC

No direct compilation dependencies exist between the Go TUI and C++ components.

## Troubleshooting

### Common Issues

1. **Go not found**
   ```bash
   ./scripts/install-go-deps.sh
   ```

2. **TUI not built**
   ```bash
   ./scripts/build-tui.sh
   ```

3. **RPC connection failed**
   - Ensure the Fuego daemon is running
   - Check firewall settings
   - Verify RPC port configurations

### Debug Mode

Enable verbose logging by setting the environment variable:

```bash
FUEGO_TUI_DEBUG=1 ./build/release/tui/build/fuego-tui
```

## Future Enhancements

1. **Additional Privacy Features**
   - Tor integration for RPC connections
   - Request padding to obscure operations
   - Mixnet integration for wallet communications

2. **Enhanced User Experience**
   - Configuration file support
   - Theming system
   - Accessibility features

3. **Staking Improvements**
   - Automated stake management
   - Cold staking support
   - Multi-signature operations

4. **Advanced Burn2Mint**
   - Direct Arbitrum integration
   - Automatic gas optimization
   - Batch operations