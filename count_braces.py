
def count_braces(filename, start_line):
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    content = "".join(lines[start_line-1:])
    open_braces = content.count('{')
    close_braces = content.count('}')
    return open_braces, close_braces

open_26, close_26 = count_braces('/Users/aejt/fuego_WS/src/Wallet/WalletRpcServerCommandsDefinitions.h', 26)
print(f"From line 26: {{={open_26}, }}={close_26}")

open_28, close_28 = count_braces('/Users/aejt/fuego_WS/src/Wallet/WalletRpcServerCommandsDefinitions.h', 28)
print(f"From line 28: {{={open_28}, }}={close_28}")
