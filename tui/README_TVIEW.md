# Fuego TUI - tview Edition

This is a new Terminal User Interface for Fuego built with the tview library, providing enhanced widgets and multi-step workflows for burn2mint, send transactions, and elderfier dashboard.

## Features

### Enhanced UI Components
- **Multi-step wizards** with proper form handling
- **Network selection** between Mainnet and Testnet
- **Modal dialogs** for confirmations and messages
- **Progress indicators** for long-running operations
- **Form-based inputs** for better user experience

### Core Functionality
- **Node Management**: Start/stop Fuego node with status monitoring
- **Wallet Operations**: Create wallet, get balance, send transactions
- **Elderfier Dashboard**: Full read/write access with voting and consensus powers
- **Burn2Mint Flow**: Complete XFG → HEAT minting process with STARK proof generation
- **Network Switching**: Seamless switching between Mainnet and Testnet

### Multi-step Menus

#### Send Transaction Wizard
1. Form-based input for recipient address and amount
2. Confirmation dialog before sending
3. Transaction hash display upon completion

#### Elderfier Dashboard
- **For Non-Stakers**:
  - Elderfyre Stayking Process wizard
  - Stake status checking
  
- **For Active Elderfiers**:
  - Consensus requests viewing
  - Voting on pending items
  - Burn2Mint request review
  - Stake management
  - ENindex key updates

#### Burn2Mint Process
1. **Burn Selection**: Choose between small (0.8 XFG) or large (800 XFG) burns
2. **Confirmation**: Modal dialog to confirm the burn amount
3. **Transaction Creation**: Creates burn deposit transaction
4. **Progress Indicator**: Visual progress during confirmation waiting
5. **Consensus Request**: Requests Elderfier consensus proof
6. **STARK Generation**: Generates STARK proof using xfg-stark CLI
7. **Completion**: Shows next steps for L2 submission

## Prerequisites

- Go 1.20 or higher
- Fuego node binaries (`fuegod` and `walletd`) built in `../build/src/` directory
- Optional: `xfg-stark` CLI for Burn2Mint STARK proof generation

## Building

```bash
cd tui
./build_tview.sh
```

This will create the `fuego-tui-tview` binary.

## Running

```bash
./fuego-tui-tview
```

### Navigation

- **Arrow Keys**: Navigate menu items and form fields
- **Enter**: Select menu items or activate buttons
- **Tab**: Move between form fields
- **Esc**: Close dialogs and return to previous screens

## Key Improvements Over Bubble Tea Version

1. **Better Form Handling**: Proper input fields with labels and validation
2. **Enhanced Visual Feedback**: Progress bars and modal dialogs
3. **Multi-step Wizards**: Guided processes for complex operations
4. **Network Selection**: Easy switching between Mainnet and Testnet
5. **Improved Layouts**: Better organized screens with clear sections
6. **Consistent UI Patterns**: Standardized dialogs and workflows

## Development

### Adding New Features

1. Create new functions for UI components using tview primitives
2. Add menu items to the main menu in `createMainMenu()`
3. Follow the existing patterns for modals, forms, and progress indicators

### UI Components Used

- **Flex**: For layout management
- **List**: For menu navigation
- **Form**: For data input
- **InputField**: For text entry
- **DropDown**: For selection options
- **TextView**: For displaying information
- **Modal**: For dialogs and messages
- **Button**: For actions

### Architecture

The application follows a state-based approach with:
- **AppState**: Central state management
- **Page-based Navigation**: Using tview.Pages for screen management
- **RPC Integration**: Direct calls to Fuego node and wallet RPC endpoints
- **Background Processing**: Goroutines for long-running operations

## Troubleshooting

### Binary Not Found
If you get "binary not found" errors:
1. Ensure `fuegod` and `walletd` are built in `../build/src/`
2. Or ensure they're available in your system PATH

### Connection Issues
- Make sure no other instances of the node/wallet are running
- Check that the required ports are available
- Verify firewall settings if connecting remotely

## Future Enhancements

- [ ] Add more detailed logging views
- [ ] Implement tabbed interfaces for complex dashboards
- [ ] Add color themes and customization options
- [ ] Implement table views for structured data
- [ ] Add keyboard shortcuts for common operations