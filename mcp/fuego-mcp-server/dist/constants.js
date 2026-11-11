import { resolve, dirname } from "path";
import { fileURLToPath } from "url";
const __dirname = dirname(fileURLToPath(import.meta.url));
// Default: dist/ → mcp/fuego-mcp-server/ → mcp/ → fuego project root
const DEFAULT_FUEGO_ROOT = resolve(__dirname, "..", "..", "..");
export const FUEGO_ROOT = process.env["FUEGO_ROOT"] ?? DEFAULT_FUEGO_ROOT;
export const CHARACTER_LIMIT = 25000;
//# sourceMappingURL=constants.js.map