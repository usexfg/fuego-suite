// ── Hearth Exchange Page ───────────────────────────────────────────────────────
'use strict';

const Hearth = (() => {
  let priceChart, candleSeries, volumeSeries, depthChart;
  let currentTf = '1h';
  let orderSide = 0; // 0=buy, 1=sell

  // ── Charts ──

  function initPriceChart() {
    const container = document.getElementById('price-chart');
    priceChart = LightweightCharts.createChart(container, {
      layout: { background: { color: '#0a0a0f' }, textColor: '#8888a0' },
      grid: { vertLines: { color: 'rgba(42,42,58,0.4)' }, horzLines: { color: 'rgba(42,42,58,0.4)' } },
      crosshair: { mode: LightweightCharts.CrosshairMode.Normal },
      rightPriceScale: { borderColor: '#2a2a3a' },
      timeScale: { borderColor: '#2a2a3a', timeVisible: true },
      width: container.clientWidth,
      height: 360
    });
    candleSeries = priceChart.addCandlestickSeries({
      upColor: '#00d4aa', downColor: '#ff4757',
      borderUpColor: '#00d4aa', borderDownColor: '#ff4757',
      wickUpColor: '#00d4aa', wickDownColor: '#ff4757'
    });
    volumeSeries = priceChart.addHistogramSeries({
      color: 'rgba(74,158,255,0.3)',
      priceFormat: { type: 'volume' },
      priceScaleId: 'vol'
    });
    priceChart.priceScale('vol').applyOptions({ scaleMargins: { top: 0.85, bottom: 0 } });
    new ResizeObserver(() => {
      priceChart.applyOptions({ width: container.clientWidth });
    }).observe(container);
  }

  function initDepthChart() {
    const container = document.getElementById('depth-chart');
    depthChart = LightweightCharts.createChart(container, {
      layout: { background: { color: '#0a0a0f' }, textColor: '#8888a0' },
      grid: { vertLines: { visible: false }, horzLines: { color: 'rgba(42,42,58,0.3)' } },
      rightPriceScale: { borderColor: '#2a2a3a' },
      timeScale: { visible: false },
      width: container.clientWidth,
      height: 240
    });
    new ResizeObserver(() => {
      depthChart.applyOptions({ width: container.clientWidth });
    }).observe(container);
  }

  async function loadOHLCV(timeframe) {
    currentTf = timeframe;
    try {
      const candles = await App.rpc('get_ohlvc', { timeframe, count: 200 });
      if (candles && candles.candles) {
        const ohlc = candles.candles.map(c => ({
          time: c.t, open: c.o / App.COIN, high: c.h / App.COIN,
          low: c.l / App.COIN, close: c.c / App.COIN
        }));
        const vol = candles.candles.map(c => ({
          time: c.t, value: c.v / App.COIN, color: c.c >= c.o ? 'rgba(0,212,170,0.3)' : 'rgba(255,71,87,0.3)'
        }));
        candleSeries.setData(ohlc);
        volumeSeries.setData(vol);
      }
    } catch (e) {
      console.warn('OHLCV load failed:', e);
    }
  }

  // ── Orderbook ──

  async function loadOrderbook() {
    try {
      const data = await App.rpc('get_orderbook_state', { depth: 15 });
      renderOrderbook(data);
      renderDepthChart(data);
    } catch (e) {
      console.warn('Orderbook load failed:', e);
    }
  }

  function renderOrderbook(data) {
    const asksBody = document.getElementById('ob-asks');
    const bidsBody = document.getElementById('ob-bids');
    const spreadEl = document.getElementById('ob-spread');

    if (!data.ask_prices || data.ask_prices.length === 0) {
      asksBody.innerHTML = '<tr><td colspan="3" style="text-align:center;color:var(--text-muted);padding:24px;">No asks</td></tr>';
      bidsBody.innerHTML = '<tr><td colspan="3" style="text-align:center;color:var(--text-muted);padding:24px;">No bids</td></tr>';
      spreadEl.textContent = 'Spread: —';
      return;
    }

    const maxAskDepth = Math.max(...(data.ask_depths || [1]));
    const maxBidDepth = Math.max(...(data.bid_depths || [1]));

    // Asks (reversed so lowest is closest to center)
    let askHtml = '';
    let askTotal = 0;
    for (let i = (data.ask_prices || []).length - 1; i >= 0; i--) {
      const price = data.ask_prices[i];
      const amount = (data.ask_amounts || data.ask_depths || [])[i] || 0;
      askTotal += amount;
      const pct = ((data.ask_depths || [])[i] || 0) / maxAskDepth * 100;
      askHtml += `<tr style="position:relative;">
        <td class="ask">${App.fmtPrice(price)}</td>
        <td style="text-align:right">${App.fmtXfg(amount)}</td>
        <td style="text-align:right">${App.fmtXfg(askTotal)}</td>
        <td class="depth-bar ask" style="width:${pct}%"></td>
      </tr>`;
    }
    asksBody.innerHTML = askHtml;

    // Bids
    let bidHtml = '';
    let bidTotal = 0;
    for (let i = 0; i < (data.bid_prices || []).length; i++) {
      const price = data.bid_prices[i];
      const amount = (data.bid_amounts || data.bid_depths || [])[i] || 0;
      bidTotal += amount;
      const pct = ((data.bid_depths || [])[i] || 0) / maxBidDepth * 100;
      bidHtml += `<tr style="position:relative;">
        <td class="bid">${App.fmtPrice(price)}</td>
        <td style="text-align:right">${App.fmtXfg(amount)}</td>
        <td style="text-align:right">${App.fmtXfg(bidTotal)}</td>
        <td class="depth-bar bid" style="width:${pct}%"></td>
      </tr>`;
    }
    bidsBody.innerHTML = bidHtml;

    // Spread
    const bestBid = data.bid_prices && data.bid_prices.length > 0 ? data.bid_prices[0] : 0;
    const bestAsk = data.ask_prices && data.ask_prices.length > 0 ? data.ask_prices[data.ask_prices.length - 1] : 0;
    if (bestBid && bestAsk) {
      const spread = (bestAsk - bestBid) / App.COIN;
      const spreadPct = ((bestAsk - bestBid) / bestAsk * 100).toFixed(3);
      spreadEl.innerHTML = `Spread: <span class="accent">${spread.toFixed(4)}</span> (${spreadPct}%)`;
    }
  }

  function renderDepthChart(data) {
    if (!data.bid_prices || !data.ask_prices) return;

    // Build cumulative depth series
    const bids = [];
    let cumBid = 0;
    for (let i = 0; i < data.bid_prices.length; i++) {
      cumBid += (data.bid_depths || [])[i] || 0;
      bids.push({ time: data.bid_prices[i] / App.COIN, value: cumBid / App.COIN });
    }
    bids.reverse();

    const asks = [];
    let cumAsk = 0;
    for (let i = data.ask_prices.length - 1; i >= 0; i--) {
      cumAsk += (data.ask_depths || [])[i] || 0;
      asks.push({ time: data.ask_prices[i] / App.COIN, value: cumAsk / App.COIN });
    }

    // Use area series for depth
    const existingSeries = depthChart._series || [];
    existingSeries.forEach(s => { try { depthChart.removeSeries(s); } catch {} });

    if (bids.length > 0) {
      const bidSeries = depthChart.addAreaSeries({
        lineColor: '#00d4aa', topColor: 'rgba(0,212,170,0.2)', bottomColor: 'rgba(0,212,170,0.02)',
        lineWidth: 1, priceLineVisible: false, lastValueVisible: false
      });
      bidSeries.setData(bids);
    }
    if (asks.length > 0) {
      const askSeries = depthChart.addAreaSeries({
        lineColor: '#ff4757', topColor: 'rgba(255,71,87,0.2)', bottomColor: 'rgba(255,71,87,0.02)',
        lineWidth: 1, priceLineVisible: false, lastValueVisible: false
      });
      askSeries.setData(asks);
    }
  }

  // ── Pool Metrics ──

  function updatePoolInfo(data) {
    if (!data) return;
    document.getElementById('spot-price').textContent = App.fmtPrice(data.spot_price);
    document.getElementById('pool-xfg').textContent = App.fmtXfg(data.reserve_xfg);
    document.getElementById('pool-heat').textContent = App.fmtHeat(data.reserve_heat);
    document.getElementById('pool-shares').textContent = data.total_lp_shares != null ? Number(data.total_lp_shares).toLocaleString() : '—';
    document.getElementById('pool-fees-xfg').textContent = App.fmtXfg(data.accumulated_lp_fees);
    document.getElementById('pool-epoch-fees').textContent = App.fmtXfg(data.epoch_swap_fees);
  }

  function updateHeatMetrics(data) {
    if (!data) return;
    document.getElementById('heat-supply').textContent = App.fmtHeat(data.heat_supply || data.total_supply);
    document.getElementById('heat-redemption').textContent = App.fmtPrice(data.redemption_price);
    document.getElementById('heat-burned').textContent = App.fmtXfg(data.xfg_burned || data.total_burned);
    document.getElementById('fee-pool').textContent = App.fmtXfg(data.fee_pool);
  }

  // ── Order Form ──

  function initOrderForm() {
    // Side tabs
    document.querySelectorAll('.tabs .tab').forEach(tab => {
      tab.addEventListener('click', () => {
        document.querySelectorAll('.tabs .tab').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        orderSide = tab.dataset.side === 'buy' ? 0 : 1;
        document.getElementById('order-preview-btn').className =
          `btn btn-block ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
        document.getElementById('order-preview-btn').textContent =
          orderSide === 0 ? 'Preview Buy' : 'Preview Sell';
      });
    });

    // Preview button
    document.getElementById('order-preview-btn').addEventListener('click', showOrderPreview);

    // Modal close button
    document.getElementById('order-modal-close').addEventListener('click', () => {
      document.getElementById('order-modal').classList.remove('active');
    });

    // Execute via wallet RPC (keys never touch browser)
    document.getElementById('order-exec-btn').addEventListener('click', async () => {
      const amount = document.getElementById('order-amount').value;
      const price = document.getElementById('order-price').value;
      const expiry = parseInt(document.getElementById('order-expiry').value) || 0;

      if (!amount || !price) { App.showToast('Fill in amount and price'); return; }

      try {
        const result = await App.walletRpc('place_limit_order', {
          side: orderSide,
          amount: Math.round(parseFloat(amount) * App.COIN),
          target_price: Math.round(parseFloat(price) * App.COIN),
          expiration: expiry,
          fee: 0,
          mixin: 0
        });
        App.showToast(`Order placed! tx: ${result.tx_hash ? result.tx_hash.substring(0, 16) + '...' : 'sent'}`);
        document.getElementById('order-modal').classList.remove('active');
      } catch (e) {
        App.showToast(`Error: ${e.message}`);
      }
    });

    // Copy CLI command
    document.getElementById('order-copy-btn').addEventListener('click', () => {
      const cmd = document.getElementById('order-cli-cmd');
      App.copyToClipboard(cmd.textContent);
    });
  }

  function showOrderPreview() {
    const amount = document.getElementById('order-amount').value;
    const price = document.getElementById('order-price').value;
    const expiry = document.getElementById('order-expiry').value || '4320';

    if (!amount || !price) { App.showToast('Fill in amount and price'); return; }

    const side = orderSide === 0 ? 'buy' : 'sell';
    const details = document.getElementById('order-modal-details');
    details.innerHTML = `
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;">
        <div class="metric"><span class="metric-label">Side</span><span class="metric-value ${orderSide === 0 ? 'green' : 'red'}">${orderSide === 0 ? 'BUY' : 'SELL'}</span></div>
        <div class="metric"><span class="metric-label">Amount</span><span class="metric-value">${amount}</span></div>
        <div class="metric"><span class="metric-label">Price</span><span class="metric-value accent">${price}</span></div>
        <div class="metric"><span class="metric-label">Expiry</span><span class="metric-value">${expiry} blocks</span></div>
      </div>`;

    const atomicAmount = Math.round(parseFloat(amount) * App.COIN);
    const atomicPrice = Math.round(parseFloat(price) * App.COIN);
    document.getElementById('order-cli-cmd').textContent =
      `fire_wallet place_order ${side} ${amount} ${price} ${expiry}`;

    document.getElementById('order-modal').classList.add('active');
  }

  // ── AMM ──

  function initAMM() {
    document.getElementById('amm-swap-btn').addEventListener('click', async () => {
      const direction = parseInt(document.getElementById('amm-direction').value);
      const amount = document.getElementById('amm-amount').value;
      if (!amount) { App.showToast('Enter an amount'); return; }

      try {
        const atomicAmount = Math.round(parseFloat(amount) * App.COIN);
        const result = await App.walletRpc('amm_swap', {
          direction,
          input_amount: atomicAmount,
          expected_output: 0,
          min_output: 0,
          fee: 0,
          mixin: 0
        });
        App.showToast(`Swap submitted! tx: ${result.tx_hash ? result.tx_hash.substring(0, 16) + '...' : 'sent'}`);
      } catch (e) {
        App.showToast(`Swap error: ${e.message}`);
      }
    });
  }

  // ── Event Handlers ──

  function init() {
    initPriceChart();
    initDepthChart();
    initOrderForm();
    initAMM();

    // Load initial data
    loadOHLCV(currentTf);
    loadOrderbook();

    // OHLCV timeframe buttons
    document.querySelectorAll('.ohlcv-tf').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.ohlcv-tf').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        loadOHLCV(btn.dataset.tf);
      });
    });

    // WebSocket events
    App.on('pool_info', updatePoolInfo);
    App.on('heat_metric', updateHeatMetrics);
    App.on('block', () => {
      loadOrderbook();
      loadOHLCV(currentTf);
    });

    // Periodic refresh
    setInterval(loadOrderbook, 10000);
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', Hearth.init);
