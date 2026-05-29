import discord
import aiohttp
import os

FUEGO_RPC = os.getenv("FUEGO_RPC", "http://127.0.0.1:18180")
# HTTP Basic Auth if configured: "http://user:pass@127.0.0.1:18180"
PAIRS = {0: "SOL", 1: "ETH", 2: "XMR", 3: "BCH"}
FEE_POOL_PRECISION = 1_000_000

bot = discord.Bot()

async def rpc_get(path: str) -> dict:
    async with aiohttp.ClientSession() as s:
        async with s.get(f"{FUEGO_RPC}{path}", timeout=aiohttp.ClientTimeout(total=10)) as r:
            return await r.json()

@bot.slash_command(name="offers", description="List atomic swap offers for a pair")
async def offers(ctx: discord.ApplicationContext,
    pair: discord.Option(int, "0=SOL 1=ETH 2=XMR 3=BCH", min_value=0, max_value=3)):
    data = await rpc_get(f"/getswapoffers?pair={pair}")
    if not data.get("offers"):
        return await ctx.respond(f"No offers for {PAIRS[pair]}")

    embed = discord.Embed(title=f"Atomic Swap Offers: {PAIRS[pair]}", color=0x00ff88)
    for o in data["offers"][:25]:
        xfg = int(o["xfgAmount"]) / 1e7
        rate = int(o["rateNum"]) / 1e7
        embed.add_field(
            name=f"{xfg:,.4f} XFG",
            value=f"Rate: {rate:.4f} XFG per {PAIRS[pair]} | Exp: {o['ttlBlocks']} blocks",
            inline=False,
        )
    await ctx.respond(embed=embed)

@bot.slash_command(name="market", description="HearthAMM ratio, burn rate, XFG price, CD APY")
async def market(ctx: discord.ApplicationContext):
    price_data = await rpc_get("/getswapprice?pair=0")
    metrics = await rpc_get("/heat_metrics")
    epoch_data = await rpc_get("/get_epoch_history?count=1")
    pool_data = await rpc_get("/amm_pool_info")

    embed = discord.Embed(title="Fuego Market Overview", color=0x00aaff)

    hs = float(price_data.get("xfgUsdMid", "0"))
    if hs > 0:
        embed.add_field(name="XFG Price (USD)", value=f"${hs:,.6f}", inline=True)
    hratio = float(price_data.get("hearthRatio", "0"))
    if hratio > 0:
        embed.add_field(name="HEAT/XFG Ratio (AMM)", value=f"{1/hratio:.5f} XFG", inline=True)
        heat_usd = float(price_data.get("heatUsd", "0"))
        if heat_usd > 0:
            embed.add_field(name="HEAT Price (USD)", value=f"${heat_usd:,.6f}", inline=True)

    num = int(metrics.get("redemption_price_num", 0))
    denom = int(metrics.get("redemption_price_denom", 1))
    if denom > 0:
        burn_rate = num / denom
        embed.add_field(name="Burn-to-Mint Rate", value=f"{burn_rate:.4f} HEAT/XFG", inline=True)

    if pool_data and pool_data.get("status") == "OK":
        rx = int(pool_data.get("reserve_xfg", 0))
        rh = int(pool_data.get("reserve_heat", 0))
        if rx > 0:
            ratio = rh / rx
            embed.add_field(name="AMM Pool Ratio", value=f"{ratio:.4f} HEAT/XFG", inline=True)

    epochs = epoch_data.get("epochs", [])
    if epochs:
        fee_rate = int(epochs[0].get("fee_rate_fixed_point", 0))
        apy_pct = (fee_rate / FEE_POOL_PRECISION) * 100
        epoch_num = epochs[0].get("epoch_number", 0)
        embed.add_field(name=f"HEAT CD APY (Epoch {epoch_num})", value=f"{apy_pct:.2f}%", inline=True)

    embed.set_footer(text="Data from fuegod RPC")
    await ctx.respond(embed=embed)

bot.run(os.getenv("DISCORD_TOKEN"))
