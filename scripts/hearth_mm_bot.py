#!/usr/bin/env python3
"""
Fuego Hearth Exchange — Automated Market Maker & Liquidity Bot

Maintains two-sided liquidity grid (XFG/HEAT) on-chain and generates constant trading volume
by interacting with Hearth AMM pool and resting limit deposit commitments.

Usage:
    python3 hearth_mm_bot.py [options]

Options:
    --dashboard-url  Fuego Dashboard base URL (default: http://127.0.0.1:18918)
    --wallet-url     Wallet RPC URL (default: http://127.0.0.1:18183/json_rpc)
    --daemon-url     Daemon RPC URL (default: http://127.0.0.1:18180)
    --spread-bps     Target grid spread in basis points (default: 50)
    --grid-levels    Number of price levels on each side (default: 3)
    --order-amount   Base order size in XFG (default: 10.0)
    --ttl-blocks     Order expiration in blocks (default: 144)
    --swap-interval  Interval in seconds between AMM volume swaps (default: 30)
    --swap-amount    Volume swap size in XFG (default: 1.0)
    --dry-run        Simulate order placement and swaps without submitting txs
"""

import argparse
import json
import logging
import signal
import sys
import time
import urllib.error
import urllib.request

COIN = 10000000  # 1e7 atomic units (7 decimals)
ORDER_PRICE_TICK = 100000  # 0.01 tick scale

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s - %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger("HearthMMBot")

class HearthBot:
    def __init__(self, args):
        self.dashboard_url = args.dashboard_url.rstrip('/')
        self.wallet_url = args.wallet_url
        self.daemon_url = args.daemon_url.rstrip('/')
        self.spread_bps = args.spread_bps
        self.grid_levels = args.grid_levels
        self.order_amount = int(args.order_amount * COIN)
        self.ttl_blocks = args.ttl_blocks
        self.swap_interval = args.swap_interval
        self.swap_amount_xfg = args.swap_amount
        self.dry_run = args.dry_run
        self.running = True
        self.last_swap_time = 0

    def http_get(self, url):
        req = urllib.request.Request(url, headers={'User-Agent': 'HearthMMBot/1.0'})
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read().decode('utf-8'))

    def http_post(self, url, payload):
        data = json.dumps(payload).encode('utf-8')
        req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode('utf-8'))

    def wallet_rpc(self, method, params=None):
        payload = {
            "jsonrpc": "2.0",
            "id": "hearth_mm",
            "method": method,
            "params": params or {}
        }
        try:
            res = self.http_post(f"{self.dashboard_url}/api/wallet", payload)
            if "error" in res:
                raise Exception(res["error"].get("message", str(res["error"])))
            return res.get("result", {})
        except Exception:
            res = self.http_post(self.wallet_url, payload)
            if "error" in res:
                raise Exception(res["error"].get("message", str(res["error"])))
            return res.get("result", {})

    def get_pool_info(self):
        try:
            return self.http_get(f"{self.dashboard_url}/amm_pool_info")
        except Exception:
            return self.http_get(f"{self.daemon_url}/amm_pool_info")

    def get_heat_metrics(self):
        try:
            return self.http_get(f"{self.dashboard_url}/heat_metrics")
        except Exception:
            return self.http_get(f"{self.daemon_url}/heat_metrics")

    def get_existing_active_orders(self):
        active_pairs = set()  # (side, target_price)
        try:
            res = self.wallet_rpc("get_limit_orders")
            orders = res.get("orders", [])
            for o in orders:
                if not o.get("withdrawn", False) and o.get("amount", 0) > 0:
                    active_pairs.add((o.get("side"), o.get("target_price")))
        except Exception as e:
            logger.debug(f"Failed to fetch existing limit orders: {e}")
        return active_pairs

    def align_price(self, price_atomic):
        tick = ORDER_PRICE_TICK
        aligned = ((price_atomic + tick // 2) // tick) * tick
        return max(tick, aligned)

    def place_grid_orders(self, spot_price):
        if spot_price <= 0:
            return

        active_existing = self.get_existing_active_orders()
        logger.info(f"Spot price: {spot_price / COIN:.5f} HEAT/XFG. Checking grid...")

        for lvl in range(1, self.grid_levels + 1):
            offset_bps = self.spread_bps * lvl

            # BUY order (Side 0): bid lower than spot
            bid_price = self.align_price(int(spot_price * (1.0 - offset_bps / 10000.0)))
            if (0, bid_price) in active_existing:
                logger.debug(f"  [BUY Level {lvl}] Active order already exists at {bid_price / COIN:.5f}")
            else:
                if self.dry_run:
                    logger.info(f"  [DRY-RUN BUY Level {lvl}] Price: {bid_price / COIN:.5f} | Amount: {self.order_amount / COIN:.2f} XFG")
                else:
                    try:
                        res = self.wallet_rpc("place_limit_order", {
                            "side": 0,
                            "amount": self.order_amount,
                            "target_price": bid_price,
                            "expiration": self.ttl_blocks,
                            "fee": 0, "mixin": 0
                        })
                        tx_hash = res.get("tx_hash", "submitted")
                        logger.info(f"  [BUY Level {lvl}] Price: {bid_price / COIN:.5f} | Amount: {self.order_amount / COIN:.2f} XFG | Tx: {tx_hash[:16]}...")
                    except Exception as e:
                        logger.warning(f"  [BUY Level {lvl}] Failed: {e}")

            # SELL order (Side 1): ask higher than spot
            ask_price = self.align_price(int(spot_price * (1.0 + offset_bps / 10000.0)))
            if (1, ask_price) in active_existing:
                logger.debug(f"  [SELL Level {lvl}] Active order already exists at {ask_price / COIN:.5f}")
            else:
                if self.dry_run:
                    logger.info(f"  [DRY-RUN SELL Level {lvl}] Price: {ask_price / COIN:.5f} | Amount: {self.order_amount / COIN:.2f} XFG")
                else:
                    try:
                        res = self.wallet_rpc("place_limit_order", {
                            "side": 1,
                            "amount": self.order_amount,
                            "target_price": ask_price,
                            "expiration": self.ttl_blocks,
                            "fee": 0, "mixin": 0
                        })
                        tx_hash = res.get("tx_hash", "submitted")
                        logger.info(f"  [SELL Level {lvl}] Price: {ask_price / COIN:.5f} | Amount: {self.order_amount / COIN:.2f} XFG | Tx: {tx_hash[:16]}...")
                    except Exception as e:
                        logger.warning(f"  [SELL Level {lvl}] Failed: {e}")

    def execute_volume_swap(self, spot_price, direction=0):
        try:
            if direction == 0:
                # XFG -> HEAT: input_amount is in XFG atomic units
                input_amount = int(self.swap_amount_xfg * COIN)
                side_str = "XFG -> HEAT"
                log_amt = f"{self.swap_amount_xfg:.2f} XFG"
            else:
                # HEAT -> XFG: input_amount is in HEAT atomic units
                heat_amount = self.swap_amount_xfg * (spot_price / COIN)
                input_amount = int(heat_amount * COIN)
                side_str = "HEAT -> XFG"
                log_amt = f"{heat_amount:.2f} HEAT"

            if self.dry_run:
                logger.info(f"[DRY-RUN Hearth Swap] Swapping {log_amt} ({side_str})")
                return

            res = self.wallet_rpc("amm_swap", {
                "direction": direction,
                "input_amount": input_amount,
                "expected_output": 0,
                "min_output": 0,
                "fee": 0, "mixin": 0
            })
            tx = res.get("tx_hash", "submitted")
            logger.info(f"[Hearth Swap] Swapped {log_amt} ({side_str}) | Tx: {tx[:16]}...")
        except Exception as e:
            logger.warning(f"[Hearth Swap] Failed: {e}")

    def harvest_and_clean_orders(self):
        try:
            res = self.wallet_rpc("get_limit_orders")
            orders = res.get("orders", [])
            for o in orders:
                order_id = o.get("order_id")
                withdrawn = o.get("withdrawn", False)
                amount = o.get("amount", 0)
                proceeds_xfg = o.get("proceeds_xfg", 0)
                proceeds_heat = o.get("proceeds_heat", 0)

                if not withdrawn and (amount == 0 or proceeds_xfg > 0 or proceeds_heat > 0):
                    if self.dry_run:
                        logger.info(f"[DRY-RUN Harvest] Would claim proceeds for order {order_id[:16]}... (XFG: {proceeds_xfg/COIN:.4f}, HEAT: {proceeds_heat/COIN:.4f})")
                    else:
                        logger.info(f"[Harvest] Claiming proceeds for order {order_id[:16]}... (XFG: {proceeds_xfg/COIN:.4f}, HEAT: {proceeds_heat/COIN:.4f})")
                        try:
                            self.wallet_rpc("cancel_limit_order", {"order_id": order_id})
                        except Exception as ce:
                            logger.warning(f"  Claim error: {ce}")
        except Exception as e:
            logger.debug(f"Harvest check skipped: {e}")

    def run(self):
        logger.info("=== Starting Hearth Exchange & Liquidity Bot ===")
        logger.info(f"Targeting Dashboard at {self.dashboard_url}")
        logger.info(f"Grid: {self.grid_levels} levels | Spread: {self.spread_bps} BPS | Order Size: {self.order_amount/COIN:.2f} XFG")
        if self.dry_run:
            logger.info("Running in DRY-RUN mode (no transactions will be submitted)")

        swap_dir = 0
        while self.running:
            try:
                pool = self.get_pool_info()
                spot_price = pool.get("spot_price", 0)

                if spot_price > 0:
                    self.place_grid_orders(spot_price)

                # Harvest claimable proceeds from filled/expired orders
                self.harvest_and_clean_orders()

                # Execute constant volume swap if interval elapsed
                now = time.time()
                if now - self.last_swap_time >= self.swap_interval:
                    if spot_price > 0:
                        self.execute_volume_swap(spot_price, direction=swap_dir)
                        swap_dir = 1 - swap_dir  # Alternate buy/sell
                        self.last_swap_time = now

            except Exception as e:
                logger.error(f"Bot loop error: {e}")

            time.sleep(15)

    def stop(self):
        logger.info("Stopping Hearth Market Bot...")
        self.running = False

def main():
    parser = argparse.ArgumentParser(description="Hearth Exchange Market Maker & Liquidity Bot")
    parser.add_argument("--dashboard-url", default="http://127.0.0.1:18918", help="Dashboard URL")
    parser.add_argument("--wallet-url", default="http://127.0.0.1:18183/json_rpc", help="Wallet RPC URL")
    parser.add_argument("--daemon-url", default="http://127.0.0.1:18180", help="Daemon RPC URL")
    parser.add_argument("--spread-bps", type=int, default=50, help="Grid spread in basis points")
    parser.add_argument("--grid-levels", type=int, default=3, help="Grid levels per side")
    parser.add_argument("--order-amount", type=float, default=10.0, help="Order amount in XFG")
    parser.add_argument("--ttl-blocks", type=int, default=144, help="Order TTL in blocks")
    parser.add_argument("--swap-interval", type=int, default=30, help="Volume swap interval in seconds")
    parser.add_argument("--swap-amount", type=float, default=1.0, help="Volume swap size in XFG")
    parser.add_argument("--dry-run", action="store_true", help="Simulate without placing orders or swaps")
    args = parser.parse_args()

    bot = HearthBot(args)

    def sig_handler(sig, frame):
        bot.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    bot.run()

if __name__ == "__main__":
    main()

