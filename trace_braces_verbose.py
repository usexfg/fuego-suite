
def trace_braces(filename):
    with open(filename, 'r') as f:
        lines = f.readlines()

    stack = []
    for i, line in enumerate(lines):
        line_no = i + 1
        open_count = line.count('{')
        close_count = line.count('}')
        
        for _ in range(open_count):
            stack.append(line_no)
        
        for _ in range(close_count):
            if not stack:
                print(f"Line {line_no}: Unmatched }}")
            else:
                opened_at = stack.pop()
                print(f"Line {line_no}: Matched }} with {{ from line {opened_at}")

    if stack:
        for opened_at in stack:
            print(f"Line {opened_at}: Unmatched {{")
    else:
        print("All braces matched.")

trace_braces('/Users/aejt/fuego_WS/src/Wallet/WalletRpcServerCommandsDefinitions.h')
