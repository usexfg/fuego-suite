#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { registerContextTools } from "./tools/context-tools.js";
import { registerFileTools } from "./tools/file-tools.js";
import { FUEGO_ROOT } from "./constants.js";
const server = new McpServer({
    name: "fuego-mcp-server",
    version: "1.0.0",
});
registerContextTools(server);
registerFileTools(server);
async function main() {
    console.error(`Fuego MCP server starting. FUEGO_ROOT=${FUEGO_ROOT}`);
    const transport = new StdioServerTransport();
    await server.connect(transport);
    console.error("Fuego MCP server running via stdio");
}
main().catch(error => {
    console.error("Fatal error:", error);
    process.exit(1);
});
//# sourceMappingURL=index.js.map