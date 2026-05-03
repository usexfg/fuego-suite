
import re

def find_unmatched_braces(filename):
    with open(filename, 'r') as f:
        content = f.read()

    stack = []
    for i, char in enumerate(content):
        if char == '{':
            stack.append(i)
        elif char == '}':
            if not stack:
                print(f"Unmatched '}}' at index {i}")
            else:
                stack.pop()
    
    if stack:
        for index in stack:
            line_no = content.count('\n', 0, index) + 1
            print(f"Unmatched '{{' at index {index}, line {line_no}")
    else:
        print("No unmatched braces found.")

find_unmatched_braces('/Users/aejt/fuego_WS/src/Wallet/WalletRpcServerCommandsDefinitions.h')
