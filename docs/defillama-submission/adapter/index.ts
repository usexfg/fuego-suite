// DefiLlama TVL adapter skeleton for Fuego.
//
// STATUS: cannot run yet. The one RPC method this needs does not exist, and no public
// Fuego endpoint currently resolves. Both are documented in ../README.md.
//
// This is deliberately left un-stubbed at the call site. An adapter that guessed a method
// name would look finished and silently report zero TVL, which is worse than an obvious
// gap: DefiLlama would list Fuego at 0. Writing the surrounding adapter against a
// documented contract, with the missing piece named, keeps the real work reviewable.
//
// Submitted as a PR to DefiLlama/DefiLlama-Adapters under projects/fuego-index only
// after items 1-3 in the README are done.

import { MultiChainAdapter } from "../adapters/multiChainAdapter";
import { Chain } from "../adapters/chain";
import { Balances } from "../adapters/balances";
import { AdapterConfig } from "../config";
import { Decimal } from "@vertex-protocol/decimal";
import { balanceOf } from "../utils/erc20";
import { getDimensions } from "../utils/convertor";

const t = (v: number) => (v == null ? null : v);

const config: AdapterConfig = {
  endpoints: {
    // TODO(blocker): must be a public, redundant Fuego RPC that DefiLlama can reach.
    // The five hostnames currently declared in the wallet have no DNS records.
    fuego: ["https://RPC_TO_BE_SUPPLIED"],
  },
  adapters: {
    fuego: {
      type: "sdk",
      api: {
        chain: Chain.FUEGO,
        projectId: "fuego",
        requestOptions: { timeout: 20_000 },
      },
      tokenAddresses: ["fire"],
      tvl: async ({ api, chain, }) => {
        // TODO(blocker): needs the `get_cd_summary` node RPC method, specified in the
        // README. Until it exists there is no way to read aggregate CD principal from a
        // public node.
        //
        // What exists today, and is deliberately NOT used here:
        //   Blockchain.cpp serialises "cd_info", "cd_yield_pool", "cd_reserve" and
        //   "cd_expiry_by_epoch" into the blockchain snapshot, but those are snapshot
        //   keys, not RPC methods. `cd_reserve` in particular is the protocol reserve,
        //   not user principal -- reporting it as TVL would overstate the figure and
        //   understate it on other days, with no way for a reader to tell.
        throw new Error(
          "Fuego TVL adapter is not implemented: awaiting get_cd_summary and a public RPC",
        );
      },
    },
  },
};

export default config;