// ── Hearth Exchange Page ───────────────────────────────────────────────────────
'use strict';

const Hearth = (() => {
  let priceChart;
  let currentTf = '1h';
  let orderSide = 0;   // 0=buy, 1=sell
  let orderType = 'limit'; // limit, market (market uses AMM under the hood)
  let indicators = { sma20: true, ema12: false, vol: true };
  let activeDrawTool = null;

  // ── Mock Data ──

  function mockCandles(count) {
    const now = Math.floor(Date.now() / 1000);
    const candles = [];
    let price = 1580000;
    for (let i = count; i > 0; i--) {
      const t = now - i * 3600;
      const change = (Math.random() - 0.48) * 40000;
      const open = price;
      const close = price + change;
      const high = Math.max(open, close) + Math.random() * 20000;
      const low = Math.min(open, close) - Math.random() * 20000;
      const vol = Math.floor(Math.random() * 5000 * 10000000) + 500000000;
      candles.push({ t, o: Math.floor(open), h: Math.floor(high), l: Math.floor(low), c: Math.floor(close), v: vol });
      price = close;
    }
    return { candles };
  }

  function mockOrderbook() {
    const bestBid = 1575000;
    const bestAsk = 1585000;
    const bids = [], bidAmts = [], bidDepths = [];
    const asks = [], askAmts = [], askDepths = [];
    let cumBid = 0, cumAsk = 0;
    for (let i = 0; i < 18; i++) {
      bids.push(bestBid - i * 15000);
      const amt = Math.floor(Math.random() * 800 + 200) * 10000000;
      bidAmts.push(amt);
      cumBid += amt;
      bidDepths.push(cumBid);
      asks.push(bestAsk + i * 12000);
      const aamt = Math.floor(Math.random() * 600 + 150) * 10000000;
      askAmts.push(aamt);
      cumAsk += aamt;
      askDepths.push(cumAsk);
    }
    return {
      bid_prices: bids, bid_amounts: bidAmts, bid_depths: bidDepths,
      ask_prices: asks, ask_amounts: askAmts, ask_depths: askDepths
    };
  }

  const MOCK_POOL = {
    spot_price: 1580000, reserve_xfg: 125000 * 10000000, reserve_heat: 19750000 * 10000000,
    total_lp_shares: 42000, accumulated_lp_fees: 3200 * 10000000, epoch_swap_fees: 180 * 10000000
  };

  const MOCK_HEAT = {
    heat_supply: 8500000 * 10000000, redemption_price: 1580000,
    xfg_burned: 1200000 * 10000000, fee_pool: 45000 * 10000000
  };

  // ── Charts ──

  function initPriceChart() {
    const container = document.getElementById('hearth-chart');
    priceChart = klinecharts.init(container, {
      styles: {
        grid: {
          show: true,
          horizontal: { color: 'rgba(42,42,58,0.2)' },
          vertical: { color: 'rgba(42,42,58,0.2)' }
        },
        candle: {
          type: 'candle_solid',
          bar: {
            upColor: '#e8734a',
            downColor: '#5b8def',
            noChangeColor: '#8a8a9a',
            upBorderColor: '#e8734a',
            downBorderColor: '#5b8def',
            noChangeBorderColor: '#8a8a9a',
            upWickColor: '#e8734a',
            downWickColor: '#5b8def',
            noChangeWickColor: '#8a8a9a'
          },
          areaLineSize: 1,
          priceMark: {
            high: { color: '#c0603a', textOffset: 5, textSize: 10 },
            low: { color: '#4a70b8', textOffset: 5, textSize: 10 },
            last: { upColor: '#e8734a', downColor: '#5b8def', noChangeColor: '#8a8a9a' }
          }
        },
        indicator: {
          ohlc: { upColor: '#e8734a', downColor: '#5b8def', noChangeColor: '#8a8a9a' },
          bars: [
            { color: 'rgba(100,140,200,0.5)', borderColor: 'rgba(100,140,200,0.7)' }
          ],
          lines: [
            { color: 'rgba(220,180,80,0.7)', size: 1 },
            { color: 'rgba(160,100,200,0.7)', size: 1 },
            { color: 'rgba(100,140,200,0.7)', size: 1 }
          ]
        },
        xAxis: {
          axisLine: { color: '#2a2a3a' },
          tickLine: { color: '#2a2a3a' },
          tickText: { color: '#555570', size: 10 }
        },
        yAxis: {
          axisLine: { color: '#2a2a3a' },
          tickLine: { color: '#2a2a3a' },
          tickText: { color: '#555570', size: 10 }
        },
        separator: { color: '#2a2a3a' },
        crosshair: {
          horizontal: { line: { color: '#555570' }, text: { color: '#e8e8f0', backgroundColor: '#1a1a25' } },
          vertical: { line: { color: '#555570' }, text: { color: '#e8e8f0', backgroundColor: '#1a1a25' } }
        }
      }
    });

    // Subscribe crosshair for info panel
    const crosshairEl = document.getElementById('chart-crosshair');
    priceChart.subscribeAction('onCrosshairChange', (event) => {
      if (!event || !event.data || !event.data.kLineData) {
        crosshairEl.classList.remove('visible');
        return;
      }
      crosshairEl.classList.add('visible');
      const d = event.data.kLineData;
      document.getElementById('ch-o').textContent = 'O ' + d.open.toFixed(5);
      document.getElementById('ch-h').textContent = 'H ' + d.high.toFixed(5);
      document.getElementById('ch-l').textContent = 'L ' + d.low.toFixed(5);
      document.getElementById('ch-c').textContent = 'C ' + d.close.toFixed(5);
      document.getElementById('ch-v').textContent = 'V ' + (d.volume >= 1e6 ? (d.volume / 1e6).toFixed(1) + 'M' : d.volume.toFixed(0));
      const date = new Date(d.timestamp);
      document.getElementById('ch-time').textContent = date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' }) + ' ' + date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    });

    // Indicator toggles
    document.querySelectorAll('.ind-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const ind = btn.dataset.ind;
        indicators[ind] = !indicators[ind];
        btn.classList.toggle('active', indicators[ind]);
        rebuildIndicators();
      });
    });

    // Drawing tools
    document.querySelectorAll('.draw-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tool = btn.dataset.tool;
        if (tool === 'clear') {
          priceChart.removeAllOverlay();
          document.querySelectorAll('.draw-btn').forEach(b => b.classList.remove('active'));
          activeDrawTool = null;
          return;
        }
        if (activeDrawTool === tool) {
          activeDrawTool = null;
          btn.classList.remove('active');
          return;
        }
        activeDrawTool = tool;
        document.querySelectorAll('.draw-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        startDrawing(tool);
      });
    });
  }

  function rebuildIndicators() {
    if (!priceChart) return;
    // Remove then re-add indicators
    try { priceChart.removeIndicator('candle_pane', 'MA'); } catch(e) {}
    try { priceChart.removeIndicator('candle_pane', 'EMA'); } catch(e) {}
    try { priceChart.removeIndicator('vol_pane', 'VOL'); } catch(e) {}
    if (indicators.sma20) priceChart.createIndicator('MA', false, { id: 'candle_pane' });
    if (indicators.ema12) priceChart.createIndicator('EMA', false, { id: 'candle_pane' });
    if (indicators.vol) priceChart.createIndicator('VOL', false, { id: 'vol_pane' });
  }

  function startDrawing(tool) {
    if (!activeDrawTool || !priceChart) return;
    const s = { line: { color: '#ff6b35', style: 0, size: 1 }, text: { color: '#ff6b35', size: 10 } };
    const configs = {
      segment: { name: 'segment', styles: s },
      horizontalRay: { name: 'priceLine', styles: { ...s, line: { color: '#ff6b35', style: 2, size: 1 } } },
      fibonacci: { name: 'fibonacciLine', styles: { ...s, polyline: { color: '#ffc107', style: 2, size: 1 } } },
      rectangle: { name: 'rect', styles: { ...s, polygon: { color: 'rgba(255,107,53,0.1)', borderColor: '#ff6b35' } } }
    };
    const config = configs[tool];
    if (config) {
      priceChart.createOverlay({
        ...config,
        onDrawEnd: () => {
          activeDrawTool = null;
          document.querySelectorAll('.draw-btn').forEach(b => b.classList.remove('active'));
        }
      });
    }
  }

  let rawCandles = [];

  async function loadOHLCV(timeframe) {
    currentTf = timeframe;
    let data = [];
    try {
      const candles = await App.rpc('get_ohlvc', { timeframe, count: 200 });
      if (candles && candles.candles) {
        data = candles.candles.map(c => ({
          timestamp: c.t * 1000,
          open: c.o / App.COIN,
          high: c.h / App.COIN,
          low: c.l / App.COIN,
          close: c.c / App.COIN,
          volume: c.v / App.COIN
        }));
      }
    } catch (e) { console.warn('OHLCV load failed, using mock data'); }

    if (data.length === 0) {
      const mock = mockCandles(120);
      data = mock.candles.map(c => ({
        timestamp: c.t * 1000,
        open: c.o / App.COIN,
        high: c.h / App.COIN,
        low: c.l / App.COIN,
        close: c.c / App.COIN,
        volume: c.v / App.COIN
      }));
    }

    rawCandles = data;
    priceChart.applyNewData(data);
  }

  // ── Hearth Orderbook ──

  async function loadOrderbook() {
    try {
      const data = await App.rpc('get_orderbook_state', { depth: 20 });
      renderHearthOrderbook(data);
      return;
    } catch (e) { console.warn('Orderbook load failed, using mock data'); }
    renderHearthOrderbook(mockOrderbook());
  }

  function renderHearthOrderbook(data) {
    const bidsContainer = document.getElementById('hearth-bids');
    const asksContainer = document.getElementById('hearth-asks');

    const bids = data.bid_prices || [];
    const asks = data.ask_prices || [];
    const bidAmounts = data.bid_amounts || data.bid_depths || [];
    const bidDepths = data.bid_depths || [];
    const askAmounts = data.ask_amounts || data.ask_depths || [];
    const askDepths = data.ask_depths || [];

    if (bids.length === 0 && asks.length === 0) {
      bidsContainer.innerHTML = '<div class="hearth-empty">Awaiting orders…</div>';
      asksContainer.innerHTML = '<div class="hearth-empty">Awaiting orders…</div>';
      return;
    }

    const maxBidDepth = Math.max(...bidDepths, 1);
    const maxAskDepth = Math.max(...askDepths, 1);

    let bidHtml = '';
    for (let i = 0; i < bids.length; i++) {
      const pct = ((bidDepths[i] || 0) / maxBidDepth) * 100;
      bidHtml += `<div class="hearth-order hearth-order-bid">
        <span class="hearth-order-price">${App.fmtPrice(bids[i])}</span>
        <span class="hearth-order-amount">${App.fmtXfg(bidAmounts[i] || 0)}</span>
        <div class="hearth-order-bar" style="width:${pct}%"></div>
      </div>`;
    }
    bidsContainer.innerHTML = bidHtml || '<div class="hearth-empty">No bids</div>';

    let askHtml = '';
    for (let i = asks.length - 1; i >= 0; i--) {
      const pct = ((askDepths[i] || 0) / maxAskDepth) * 100;
      askHtml += `<div class="hearth-order hearth-order-ask">
        <span class="hearth-order-price">${App.fmtPrice(asks[i])}</span>
        <span class="hearth-order-amount">${App.fmtXfg(askAmounts[i] || 0)}</span>
        <div class="hearth-order-bar" style="width:${pct}%"></div>
      </div>`;
    }
    asksContainer.innerHTML = askHtml || '<div class="hearth-empty">No asks</div>';
  }

  // ── Metrics ──

  function updatePoolInfo(data) {
    const d = data || MOCK_POOL;
    document.getElementById('pool-xfg').textContent = App.fmtXfg(d.reserve_xfg);
    document.getElementById('pool-heat').textContent = App.fmtHeat(d.reserve_heat);
    // Update the big price display
    const priceEl = document.getElementById('price-xfg-heat');
    const usdEl = document.getElementById('price-xfg-usd');
    if (priceEl) {
      const p = d.spot_price / App.COIN;
      priceEl.textContent = p.toFixed(5) + ' HΞ∆Ŧ';
      // USD: assume HEAT peg ~$1.58
      const usd = p * 1.58;
      usdEl.textContent = '≈ $' + usd.toFixed(4) + ' USD';
    }
  }

  function updateHeatMetrics(data) {
    const d = data || MOCK_HEAT;
    document.getElementById('heat-supply').textContent = App.fmtHeat(d.heat_supply || d.total_supply);
    // Mint ratio: XFG needed to mint 1 HEAT (XFG per HEAT)
    const redemptionPrice = d.redemption_price || 1580000;
    const xfgPerHeat = redemptionPrice / App.COIN;
    document.getElementById('heat-redemption').textContent = xfgPerHeat.toFixed(2) + ':1';
    document.getElementById('heat-burned').textContent = App.fmtXfg(d.xfg_burned || d.total_burned);
  }

  // ── Order Form ──

  function initOrderForm() {
    // Side tabs (Buy/Sell)
    document.querySelectorAll('.tabs .tab').forEach(tab => {
      tab.addEventListener('click', () => {
        document.querySelectorAll('.tabs .tab').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        orderSide = tab.dataset.side === 'buy' ? 0 : 1;
        updateOrderButton();
      });
    });

    // Type selector (Limit/Market/AMM)
    document.querySelectorAll('.type-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.type-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        orderType = btn.dataset.type;
        updateOrderFields();
        updateOrderButton();
      });
    });

    document.getElementById('order-preview-btn').addEventListener('click', showOrderPreview);
    document.getElementById('order-modal-close').addEventListener('click', () => {
      document.getElementById('order-modal').classList.remove('active');
    });
    document.getElementById('order-exec-btn').addEventListener('click', executeOrder);
    document.getElementById('order-copy-btn').addEventListener('click', () => {
      App.copyToClipboard(document.getElementById('order-cli-cmd').textContent);
    });

    updateOrderFields();
    updateOrderButton();
  }

  function updateOrderFields() {
    const priceGroup = document.getElementById('price-group');
    const expiryGroup = document.getElementById('expiry-group');
    if (orderType === 'market') {
      priceGroup.style.display = 'none';
      expiryGroup.style.display = 'none';
    } else {
      priceGroup.style.display = '';
      expiryGroup.style.display = '';
    }
  }

  function updateOrderButton() {
    const btn = document.getElementById('order-preview-btn');
    const side = orderSide === 0 ? 'Buy' : 'Sell';
    btn.textContent = `${side} XFG`;
    btn.className = `btn btn-primary btn-execute ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
  }

  function showOrderPreview() {
    const amount = document.getElementById('order-amount').value;
    if (!amount) { App.showToast('Enter an amount'); return; }

    const side = orderSide === 0 ? 'buy' : 'sell';
    const details = document.getElementById('order-modal-details');
    let html = `<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">`;
    html += `<div class="metric"><span class="metric-label">Type</span><span class="metric-value">${orderType.toUpperCase()}</span></div>`;
    html += `<div class="metric"><span class="metric-label">Side</span><span class="metric-value ${orderSide === 0 ? 'green' : 'red'}">${side.toUpperCase()}</span></div>`;
    html += `<div class="metric"><span class="metric-label">Amount</span><span class="metric-value">${amount} XFG</span></div>`;
    if (orderType === 'limit') {
      const price = document.getElementById('order-price').value;
      const expiry = document.getElementById('order-expiry').value || '4320';
      html += `<div class="metric"><span class="metric-label">Price</span><span class="metric-value accent">${price} HΞ∆Ŧ</span></div>`;
      html += `<div class="metric"><span class="metric-label">Expiry</span><span class="metric-value">${expiry} blocks</span></div>`;
    }
    html += `</div>`;
    details.innerHTML = html;

    const atomicAmt = Math.round(parseFloat(amount) * App.COIN);
    if (orderType === 'limit') {
      const price = document.getElementById('order-price').value;
      const expiry = document.getElementById('order-expiry').value || '4320';
      document.getElementById('order-cli-cmd').textContent =
        `fire_wallet place_order ${side} ${amount} ${price} ${expiry}`;
    } else {
      document.getElementById('order-cli-cmd').textContent =
        `fire_wallet amm_swap ${orderSide === 0 ? 0 : 1} ${atomicAmt}`;
    }

    document.getElementById('order-modal').classList.add('active');
  }

  async function executeOrder() {
    const amount = document.getElementById('order-amount').value;
    if (!amount) { App.showToast('Enter an amount'); return; }

    try {
      const atomicAmt = Math.round(parseFloat(amount) * App.COIN);
      let result;

      if (orderType === 'limit') {
        const price = document.getElementById('order-price').value;
        const expiry = parseInt(document.getElementById('order-expiry').value) || 0;
        if (!price) { App.showToast('Enter a price for limit order'); return; }
        result = await App.walletRpc('place_limit_order', {
          side: orderSide,
          amount: atomicAmt,
          target_price: Math.round(parseFloat(price) * App.COIN),
          expiration: expiry,
          fee: 0, mixin: 0
        });
      } else {
        result = await App.walletRpc('amm_swap', {
          direction: orderSide === 0 ? 0 : 1,
          input_amount: atomicAmt,
          expected_output: 0,
          min_output: 0,
          fee: 0, mixin: 0
        });
      }

      App.showToast(`Order sent! tx: ${result.tx_hash ? result.tx_hash.substring(0, 16) + '...' : 'submitted'}`);
      document.getElementById('order-modal').classList.remove('active');
    } catch (e) {
      App.showToast(`Error: ${e.message}`);
    }
  }

  // ── Init ──

  function init() {
    initPriceChart();
    initOrderForm();

    loadOHLCV(currentTf);
    loadOrderbook();
    updatePoolInfo(null);
    updateHeatMetrics(null);

    document.querySelectorAll('.ohlcv-tf').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.ohlcv-tf').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        loadOHLCV(btn.dataset.tf);
      });
    });

    App.on('pool_info', updatePoolInfo);
    App.on('heat_metric', updateHeatMetrics);
    App.on('block', () => { loadOrderbook(); loadOHLCV(currentTf); });

    setInterval(loadOrderbook, 10000);
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', Hearth.init);
