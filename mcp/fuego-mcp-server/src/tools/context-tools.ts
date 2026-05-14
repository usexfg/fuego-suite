import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import {
  ARCHITECTURE_OVERVIEW,
  MODULE_CONTEXT,
  RPC_REFERENCE,
  DATA_TYPES,
  CRYPTO_EXPLAINER,
  TOKEN_MODEL,
  BUILD_GUIDE,
} from "../context.js";

export function registerContextTools(server: McpServer): void {
  server.registerTool(
    "fuego_get_overview",
    {
      title: "Get Fuego Architecture Overview",
      description: `Returns the complete Fuego codebase architecture overview.
Covers: project purpose, tech stack, directory map, key binaries, RPC ports,
core architectural patterns, and transaction version history.

Use this first to orient yourself when starting work on this codebase.
Returns comprehensive markdown documentation.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: ARCHITECTURE_OVERVIEW }],
    })
  );

  const moduleNames = Object.keys(MODULE_CONTEXT);

  server.registerTool(
    "fuego_get_module_info",
    {
      title: "Get Fuego Module Details",
      description: `Returns detailed context about a specific Fuego module: key files, data structures,
important types, and module-specific patterns.

Available modules: ${moduleNames.join(", ")}

Args:
  - module (string): Module name from the list above (case-sensitive)

Returns: Markdown documentation for the module including file descriptions and key types.

Examples:
  - Understand transaction internals: module="CryptoNoteCore"
  - Understand ring signatures: module="Crypto"
  - Understand RPC endpoints: module="Rpc"
  - Understand wallet logic: module="Wallet"
  - Understand atomic swaps: module="SwapXFG"
  - Understand terminal UI: module="TUI"`,
      inputSchema: z.object({
        module: z.enum(moduleNames as [string, ...string[]]).describe(
          `Module name. One of: ${moduleNames.join(", ")}`
        ),
      }).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async ({ module }) => {
      const content = MODULE_CONTEXT[module];
      if (!content) {
        return {
          content: [{
            type: "text" as const,
            text: `Unknown module: ${module}. Available: ${moduleNames.join(", ")}`,
          }],
        };
      }
      return { content: [{ type: "text" as const, text: content }] };
    }
  );

  server.registerTool(
    "fuego_get_rpc_reference",
    {
      title: "Get Fuego RPC API Reference",
      description: `Returns the complete RPC API reference for all Fuego daemons.
Covers: fuegod (node daemon, port 18180), walletd (wallet daemon, port 18282),
and TUI/Elderfier RPC extensions for staking and burn2mint.

Includes method names, request parameters, and response field descriptions.
Use when implementing clients, debugging RPC calls, or understanding API capabilities.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: RPC_REFERENCE }],
    })
  );

  server.registerTool(
    "fuego_get_data_types",
    {
      title: "Get Fuego Key Data Type Definitions",
      description: `Returns C++ type definitions for all core Fuego data structures with explanations.
Covers: AccountKeys, Block, Transaction, TransactionInput variants,
TransactionOutput variants, DepositInfo, and TransactionExtra fields.

Use when you need to understand data layouts, implement serialization,
or work with the C++ codebase.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: DATA_TYPES }],
    })
  );

  server.registerTool(
    "fuego_get_crypto_explainer",
    {
      title: "Get Fuego Cryptographic Primitives Explained",
      description: `Returns explanations of all cryptographic primitives used in Fuego.
Covers: ring signatures (LSAG), stealth addresses, Pedersen commitments,
membership/tier proofs (1-of-4 OR), adaptor signatures (atomic swaps),
MLSAG (planned v11), and OSPEAD (dynamic ring size).

Each primitive includes: how it works, why it's used, and implementation notes.
Use when implementing crypto code, reviewing security properties, or understanding
how privacy is achieved.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: CRYPTO_EXPLAINER }],
    })
  );

  server.registerTool(
    "fuego_get_token_model",
    {
      title: "Get Fuego Token & Economic Model",
      description: `Returns the complete Fuego token and economic model documentation.
Covers: XFG (native coin), CD (Commitment Deposits — yield-bearing locked XFG),
HEAT (L2 token via Burn2Mint), and all DIGM platform tokens
(PARA, VOX, CURA, nfVOX, TOP).

Also covers: CD mechanics (locking, interest accrual, spending),
HEAT burn2mint flow (burn → Elder consensus → STARK proof → L2 mint),
Elderfier system (staking, governance, rewards), and swap fee distribution.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: TOKEN_MODEL }],
    })
  );

  server.registerTool(
    "fuego_get_build_guide",
    {
      title: "Get Fuego Build & Run Guide",
      description: `Returns complete build instructions, run commands, and testing procedures for Fuego.
Covers: prerequisites for Ubuntu/macOS, CMake build commands, Makefile targets,
output binary locations, runtime commands for fuegod/walletd/tui/swapxfg,
Docker deployment, test commands, and key build flags.`,
      inputSchema: z.object({}).strict(),
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    },
    async () => ({
      content: [{ type: "text" as const, text: BUILD_GUIDE }],
    })
  );
}
