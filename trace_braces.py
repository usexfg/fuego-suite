
def trace_braces(filename):
    with open(filename, 'r') as f:
        lines = f.readlines()

    stack = []
    for i, line in enumerate(lines):
        line_no = i + 1
        # This is a simple parser, it might fail on complex lines, 
        # but for our purposes it should be okay.
        # We only care about { and }
        
        # Count { in line
        open_count = line.count('{')
        # Count } in line
        close_count = line.count('}')
        
        # This is not perfect because it doesn't handle comments or strings
        # But let's try.
        
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
