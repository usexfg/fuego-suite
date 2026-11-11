import { z } from "zod";
import { exec } from "child_process";
import { promisify } from "util";
import { readFile } from "fs/promises";
import { join, resolve, relative } from "path";
import { FUEGO_ROOT, CHARACTER_LIMIT } from "../constants.js";
const execAsync = promisify(exec);
function safeRelPath(filePath) {
    // Accept both absolute and relative paths; always resolve relative to FUEGO_ROOT
    const abs = filePath.startsWith("/") ? filePath : join(FUEGO_ROOT, filePath);
    const resolved = resolve(abs);
    // Security: ensure path stays within FUEGO_ROOT
    if (!resolved.startsWith(resolve(FUEGO_ROOT))) {
        throw new Error(`Path traversal denied: ${filePath}`);
    }
    return resolved;
}
export function registerFileTools(server) {
    server.registerTool("fuego_search_code", {
        title: "Search Fuego Codebase",
        description: `Search for symbols, patterns, or text in the Fuego codebase using grep.
Returns file:line matches. Useful for finding function definitions, struct usages,
RPC method implementations, or any text pattern.

Args:
  - pattern (string): grep regex pattern to search for
  - path (string, optional): subdirectory to search within (e.g., "src/CryptoNoteCore", "tui")
  - file_glob (string, optional): limit to files matching this glob (e.g., "*.cpp", "*.h", "*.go")
  - case_sensitive (boolean, optional): default true
  - max_results (number, optional): max matches to return, default 50

Returns: List of matches in format "relative/path/file.cpp:LINE: matched line"

Examples:
  - Find TransactionOutputCommitment definition: pattern="struct TransactionOutputCommitment"
  - Find all RPC handlers: pattern="COMMAND_RPC", path="src/Rpc"
  - Find wallet balance function: pattern="getBalance", path="src/Wallet"
  - Find Go swap pairs: pattern="XFG", path="swapxfg", file_glob="*.go"`,
        inputSchema: z.object({
            pattern: z.string().min(1).describe("grep regex pattern"),
            path: z.string().optional().describe("Subdirectory to search (relative to FUEGO_ROOT)"),
            file_glob: z.string().optional().describe("File pattern, e.g. '*.cpp', '*.h', '*.go'"),
            case_sensitive: z.boolean().default(true).describe("Case-sensitive search"),
            max_results: z.number().int().min(1).max(200).default(50).describe("Max matches"),
        }).strict(),
        annotations: {
            readOnlyHint: true,
            destructiveHint: false,
            idempotentHint: true,
            openWorldHint: false,
        },
    }, async ({ pattern, path, file_glob, case_sensitive, max_results }) => {
        try {
            const searchDir = path ? safeRelPath(path) : FUEGO_ROOT;
            const caseFlag = case_sensitive ? "" : "-i";
            const includeFlag = file_glob ? `--include="${file_glob}"` : "";
            const cmd = `grep -rn ${caseFlag} ${includeFlag} --max-count=1 -l /dev/null 2>/dev/null; grep -rn ${caseFlag} ${includeFlag} "${pattern}" "${searchDir}" 2>/dev/null | head -${max_results}`;
            const cleanCmd = `grep -rn ${caseFlag} ${includeFlag} "${pattern}" "${searchDir}" 2>/dev/null | head -${max_results}`;
            const { stdout } = await execAsync(cleanCmd, { timeout: 15000 });
            if (!stdout.trim()) {
                return {
                    content: [{ type: "text", text: `No matches found for pattern: ${pattern}` }],
                };
            }
            // Make paths relative to FUEGO_ROOT for readability
            const lines = stdout.trim().split("\n").map(line => {
                const colonIdx = line.indexOf(":");
                if (colonIdx > 0) {
                    const absPath = line.substring(0, colonIdx);
                    try {
                        const rel = relative(FUEGO_ROOT, absPath);
                        return rel + line.substring(colonIdx);
                    }
                    catch {
                        return line;
                    }
                }
                return line;
            });
            const result = lines.join("\n");
            const text = result.length > CHARACTER_LIMIT
                ? result.substring(0, CHARACTER_LIMIT) + `\n\n[Truncated. Use 'path' or 'file_glob' to narrow results.]`
                : result;
            return { content: [{ type: "text", text }] };
        }
        catch (error) {
            const msg = error instanceof Error ? error.message : String(error);
            if (msg.includes("Path traversal"))
                throw error;
            return { content: [{ type: "text", text: `Search error: ${msg}` }] };
        }
    });
    server.registerTool("fuego_read_file", {
        title: "Read Fuego Source File",
        description: `Read the content of a file in the Fuego codebase.
Accepts paths relative to the project root (e.g., "src/CryptoNoteCore/Core.h")
or absolute paths within the project.

Args:
  - file_path (string): Path to file, relative to FUEGO_ROOT
  - start_line (number, optional): First line to read (1-indexed, default: 1)
  - end_line (number, optional): Last line to read (default: read up to 300 lines)

Returns: File content with line numbers

Examples:
  - Read key type definitions: file_path="src/CryptoNoteCore/CryptoNoteBasic.h"
  - Read wallet RPC: file_path="src/Rpc/CoreRpcServerCommandsDefinitions.h"
  - Read TUI config: file_path="tui/config.go"
  - Read swap pairs: file_path="swapxfg/app/pairs.go"`,
        inputSchema: z.object({
            file_path: z.string().min(1).describe("File path relative to FUEGO_ROOT"),
            start_line: z.number().int().min(1).default(1).describe("First line to read"),
            end_line: z.number().int().min(1).optional().describe("Last line to read"),
        }).strict(),
        annotations: {
            readOnlyHint: true,
            destructiveHint: false,
            idempotentHint: true,
            openWorldHint: false,
        },
    }, async ({ file_path, start_line, end_line }) => {
        try {
            const absPath = safeRelPath(file_path);
            const content = await readFile(absPath, "utf-8");
            const lines = content.split("\n");
            const maxLines = 300;
            const from = start_line - 1;
            const to = end_line !== undefined ? end_line : Math.min(from + maxLines, lines.length);
            const selectedLines = lines.slice(from, to);
            const numbered = selectedLines
                .map((line, i) => `${String(from + i + 1).padStart(5)} | ${line}`)
                .join("\n");
            const header = `// ${file_path} (lines ${from + 1}-${to} of ${lines.length})\n\n`;
            const text = header + numbered;
            if (to < lines.length && end_line === undefined) {
                return {
                    content: [{
                            type: "text",
                            text: text + `\n\n[File has ${lines.length} lines. Use start_line/end_line to read more.]`,
                        }],
                };
            }
            return { content: [{ type: "text", text }] };
        }
        catch (error) {
            const msg = error instanceof Error ? error.message : String(error);
            if (msg.includes("Path traversal")) {
                return { content: [{ type: "text", text: `Error: ${msg}` }] };
            }
            if (msg.includes("ENOENT")) {
                return { content: [{ type: "text", text: `File not found: ${file_path}` }] };
            }
            return { content: [{ type: "text", text: `Read error: ${msg}` }] };
        }
    });
    server.registerTool("fuego_find_files", {
        title: "Find Files in Fuego Codebase",
        description: `Find files by name pattern, extension, or path fragment in the Fuego codebase.

Args:
  - pattern (string): filename pattern (supports * wildcard), e.g. "*.cpp", "Blockchain*", "*Deposit*"
  - path (string, optional): subdirectory to search (e.g., "src", "tui", "swapxfg")
  - max_results (number, optional): max results, default 50

Returns: List of matching file paths relative to FUEGO_ROOT

Examples:
  - All C++ files: pattern="*.cpp"
  - All deposit-related headers: pattern="*Deposit*", path="src"
  - All Go files in swapxfg: pattern="*.go", path="swapxfg"
  - Find Currency source: pattern="Currency.*"`,
        inputSchema: z.object({
            pattern: z.string().min(1).describe("Filename glob pattern (supports *)"),
            path: z.string().optional().describe("Subdirectory to search"),
            max_results: z.number().int().min(1).max(200).default(50).describe("Max results"),
        }).strict(),
        annotations: {
            readOnlyHint: true,
            destructiveHint: false,
            idempotentHint: true,
            openWorldHint: false,
        },
    }, async ({ pattern, path, max_results }) => {
        try {
            const searchDir = path ? safeRelPath(path) : FUEGO_ROOT;
            // Exclude build dirs and external for cleaner results
            const cmd = `find "${searchDir}" -name "${pattern}" -not -path "*/build/*" -not -path "*/external/gtest*" -not -path "*/.git/*" -not -path "*/node_modules/*" 2>/dev/null | head -${max_results}`;
            const { stdout } = await execAsync(cmd, { timeout: 10000 });
            if (!stdout.trim()) {
                return {
                    content: [{ type: "text", text: `No files found matching: ${pattern}` }],
                };
            }
            const files = stdout.trim().split("\n").map(f => {
                try {
                    return relative(FUEGO_ROOT, f);
                }
                catch {
                    return f;
                }
            });
            return {
                content: [{
                        type: "text",
                        text: `Found ${files.length} file(s):\n\n${files.join("\n")}`,
                    }],
            };
        }
        catch (error) {
            const msg = error instanceof Error ? error.message : String(error);
            return { content: [{ type: "text", text: `Find error: ${msg}` }] };
        }
    });
}
//# sourceMappingURL=file-tools.js.map