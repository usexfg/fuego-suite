// ── Hearth Exchange Page ───────────────────────────────────────────────────────
'use strict';

// Keep order preparation pure so the browser and offline tests share the same
// unit, direction, and exact-atomic validation rules.
const HearthOrder = (() => {
  const COIN = 10000000n;
  const MAX_JSON_INTEGER = BigInt(Number.MAX_SAFE_INTEGER);

  function atomic(text, label) {
    const value = String(text).trim();
    if (!/^\d+(?:\.\d{1,7})?$/.test(value)) {
      throw new Error(`${label} must be a positive amount with at most 7 decimals`);
    }
    const [whole, fraction = ''] = value.split('.');
    const units = BigInt(whole) * COIN + BigInt((fraction + '0000000').slice(0, 7));
    if (units === 0n || units > MAX_JSON_INTEGER) {
      throw new Error(`${label} is zero or exceeds the dashboard's exact JSON range`);
    }
    return Number(units);
  }

  function formatAtomic(value) {
    const units = BigInt(value);
    const fraction = String(units % COIN).padStart(7, '0').replace(/0+$/, '');
    return `${units / COIN}${fraction ? '.' + fraction : ''}`;
  }

  function limit(side, amountText, priceText, expiryText) {
    const amount = atomic(amountText, side === 0 ? 'HEAT budget' : 'XFG amount');
    const targetPrice = atomic(priceText, 'HEAT/XFG price');
    if (targetPrice % 100000 !== 0) {
      throw new Error('Limit price must be a multiple of 0.01 HEAT/XFG');
    }
    const rawExpiry = String(expiryText || '0').trim();
    if (!/^\d+$/.test(rawExpiry)) throw new Error('Expiry must be an absolute block height or 0');
    const expiration = Number(rawExpiry);
    if (!Number.isSafeInteger(expiration) || expiration > 0xffffffff) {
      throw new Error('Expiry height is outside the supported range');
    }
    return {
      type: 'limit', side, inputAsset: side === 0 ? 'HEAT' : 'XFG', amount,
      params: { side, amount, target_price: targetPrice, expiration, fee: 0, mixin: 0 }
    };
  }

  function market(side, amountText, quote) {
    const direction = side === 0 ? 1 : 0; // buy XFG spends HEAT
    const inputAsset = side === 0 ? 'HEAT' : 'XFG';
    const outputAsset = side === 0 ? 'XFG' : 'HEAT';
    const inputAmount = atomic(amountText, `${inputAsset} input`);
    const expected = Number(quote && quote.expected_output);
    if (!Number.isSafeInteger(expected) || expected <= 0) {
      throw new Error('AMM returned no exact, executable output quote');
    }
    return {
      type: 'market', side, direction, inputAsset, outputAsset,
      amount: inputAmount, expected,
      // The wallet creates a fixed-output transaction, not a variable-output
      // market order. A strict floor keeps the preview and signed output equal.
      params: { direction, input_amount: inputAmount, expected_output: expected,
        min_output: expected, fee: 0, mixin: 0 }
    };
  }

  return { atomic, formatAtomic, limit, market };
})();

const Hearth = (() => {
  let priceChart;
  let currentTf = '1h';
  let orderSide = 0;   // 0=buy, 1=sell
  let orderType = 'limit'; // limit, market (market uses AMM under the hood)
  let pendingOrder = null;
  let indicators = { sma20: true, ema12: false, vol: true };
  let activeDrawTool = null;

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

  function loadOHLCV(timeframe) {
    currentTf = timeframe;
    // No daemon OHLCV endpoint exists yet. Never substitute random candles in
    // the operator dashboard; the design sandbox retains its own fixture data.
    priceChart.applyNewData([]);
    document.getElementById('hearth-chart-status').textContent =
      'Historical candles unavailable — daemon OHLCV endpoint pending';
  }

  // ── Hearth Orderbook ──

  async function loadOrderbook() {
    try {
      const data = await App.daemonPost('/get_limit_orders', {
        active_only: true, limit: 100, offset: 0
      });
      renderHearthOrderbook(data);
    } catch (e) {
      document.getElementById('hearth-bids').innerHTML =
        '<div class="hearth-empty">Live orders unavailable</div>';
      document.getElementById('hearth-asks').innerHTML =
        '<div class="hearth-empty">Live orders unavailable</div>';
    }
  }

  function renderHearthOrderbook(data) {
    const bidsContainer = document.getElementById('hearth-bids');
    const asksContainer = document.getElementById('hearth-asks');

    const orders = Array.isArray(data && data.orders) ? data.orders : [];
    const valid = orders.filter(o => (o.side === 0 || o.side === 1) &&
      Number.isFinite(Number(o.target_price)) && Number.isFinite(Number(o.amount)) &&
      Number(o.target_price) > 0 && Number(o.amount) > 0 && !o.withdrawn);
    const bids = valid.filter(o => o.side === 0)
      .sort((a, b) => Number(b.target_price) - Number(a.target_price)).slice(0, 18);
    const asks = valid.filter(o => o.side === 1)
      .sort((a, b) => Number(a.target_price) - Number(b.target_price)).slice(0, 18);

    if (valid.length === 0) {
      bidsContainer.innerHTML = '<div class="hearth-empty">Awaiting orders…</div>';
      asksContainer.innerHTML = '<div class="hearth-empty">Awaiting orders…</div>';
      return;
    }

    const bidHtml = bids.map(o => `<div class="hearth-order hearth-order-bid">
      <span class="hearth-order-price">${App.fmtPrice(o.target_price)}</span>
      <span class="hearth-order-amount">${App.fmtHeat(o.amount)} HEAT</span>
    </div>`).join('');
    bidsContainer.innerHTML = bidHtml || '<div class="hearth-empty">No bids</div>';
    const askHtml = asks.map(o => `<div class="hearth-order hearth-order-ask">
      <span class="hearth-order-price">${App.fmtPrice(o.target_price)}</span>
      <span class="hearth-order-amount">${App.fmtXfg(o.amount)} XFG</span>
    </div>`).join('');
    asksContainer.innerHTML = askHtml || '<div class="hearth-empty">No asks</div>';
  }

  // ── Metrics ──

  function updatePoolInfo(data) {
    const valid = data && data.status === 'OK' &&
      Number(data.reserve_xfg) > 0 && Number(data.reserve_heat) > 0;
    document.getElementById('pool-xfg').textContent = valid ? App.fmtXfg(data.reserve_xfg) : '—';
    document.getElementById('pool-heat').textContent = valid ? App.fmtHeat(data.reserve_heat) : '—';
    const priceEl = document.getElementById('price-xfg-heat');
    const usdEl = document.getElementById('price-xfg-usd');
    priceEl.textContent = valid && Number(data.spot_price) > 0
      ? App.fmtPrice(data.spot_price) + ' HΞ∆Ŧ / XFG' : '—';
    usdEl.textContent = 'USD quote unavailable';
    document.getElementById('hearth-market-status').textContent = valid
      ? `Live pool · height ${data.height}` : 'Live pool unavailable';
  }

  function updateHeatMetrics(data) {
    const valid = data && data.status === 'OK';
    document.getElementById('heat-supply').textContent = valid
      ? App.fmtHeat(data.heat_supply) : '—';
    document.getElementById('heat-burned').textContent = valid
      ? App.fmtXfg(data.total_burned_xfg) : '—';
    const numerator = valid ? Number(data.redemption_price_num) : 0;
    const denominator = valid ? Number(data.redemption_price_denom) : 0;
    document.getElementById('heat-redemption').textContent = numerator > 0 && denominator > 0
      ? (numerator / denominator).toFixed(4) + ' XFG/HEAT' : '—';
  }

  async function refreshMetrics() {
    const [pool, heat] = await Promise.allSettled([
      App.daemonPost('/amm_pool_info'), App.daemonPost('/heat_metrics')
    ]);
    updatePoolInfo(pool.status === 'fulfilled' ? pool.value : null);
    updateHeatMetrics(heat.status === 'fulfilled' ? heat.value : null);
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
      pendingOrder = null;
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
    document.getElementById('order-amount-label').textContent = orderSide === 0
      ? 'HEAT Budget' : 'XFG Amount';
  }

  function updateOrderButton() {
    const btn = document.getElementById('order-preview-btn');
    const side = orderSide === 0 ? 'Buy' : 'Sell';
    btn.textContent = `Preview ${side} XFG`;
    btn.className = `btn btn-primary btn-execute ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
    updateOrderFields();
  }

  async function showOrderPreview() {
    const amount = document.getElementById('order-amount').value;
    const side = orderSide;
    const type = orderType;
    const previewBtn = document.getElementById('order-preview-btn');
    previewBtn.disabled = true;
    pendingOrder = null;
    try {
      let plan;
      if (type === 'limit') {
        plan = HearthOrder.limit(side, amount,
          document.getElementById('order-price').value,
          document.getElementById('order-expiry').value);
      } else {
        const input = HearthOrder.atomic(amount, side === 0 ? 'HEAT input' : 'XFG input');
        const quote = await App.daemonPost('/amm_quote', {
          input_amount: input, direction: side === 0 ? 1 : 0
        });
        if (side !== orderSide || type !== orderType ||
            amount !== document.getElementById('order-amount').value) {
          throw new Error('Order form changed while quoting. Preview again.');
        }
        plan = HearthOrder.market(side, amount, quote);
      }
      pendingOrder = plan;
      const sideLabel = plan.side === 0 ? 'BUY XFG' : 'SELL XFG';
      let html = `<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">`;
      html += `<div class="metric"><span class="metric-label">Type</span><span class="metric-value">${plan.type.toUpperCase()}</span></div>`;
      html += `<div class="metric"><span class="metric-label">Side</span><span class="metric-value">${sideLabel}</span></div>`;
      html += `<div class="metric"><span class="metric-label">Input</span><span class="metric-value">${HearthOrder.formatAtomic(plan.amount)} ${plan.inputAsset}</span></div>`;
      if (plan.type === 'limit') {
        html += `<div class="metric"><span class="metric-label">Limit</span><span class="metric-value">${HearthOrder.formatAtomic(plan.params.target_price)} HEAT/XFG</span></div>`;
        html += `<div class="metric"><span class="metric-label">Expiry height</span><span class="metric-value">${plan.params.expiration || 'None'}</span></div>`;
      } else {
        html += `<div class="metric"><span class="metric-label">Fixed output</span><span class="metric-value">${HearthOrder.formatAtomic(plan.expected)} ${plan.outputAsset}</span></div>`;
      }
      html += '</div>';
      if (plan.type === 'market') {
        html += '<p class="hearth-empty">Exact-output AMM quote. If the pool worsens before submission, the order will be refused; there is no silent slippage.</p>';
      }
      document.getElementById('order-modal-details').innerHTML = html;
      document.getElementById('order-cli-cmd').textContent = JSON.stringify({
        method: plan.type === 'limit' ? 'place_limit_order' : 'amm_swap',
        params: plan.params
      }, null, 2);
      document.getElementById('order-modal').classList.add('active');
    } catch (e) {
      App.showToast(`Preview unavailable: ${e.message}`);
    } finally {
      previewBtn.disabled = false;
    }
  }

  async function executeOrder() {
    if (!pendingOrder) return;
    const plan = pendingOrder;
    const button = document.getElementById('order-exec-btn');
    button.disabled = true;
    try {
      if (plan.type === 'market') {
        const fresh = await App.daemonPost('/amm_quote', {
          input_amount: plan.amount, direction: plan.direction
        });
        const current = HearthOrder.market(plan.side, HearthOrder.formatAtomic(plan.amount), fresh);
        if (current.expected < plan.expected) {
          pendingOrder = null;
          document.getElementById('order-modal').classList.remove('active');
          throw new Error('Pool price moved against this quote. Preview again.');
        }
      }
      const result = await App.walletRpc(
        plan.type === 'limit' ? 'place_limit_order' : 'amm_swap', plan.params);
      App.showToast(`Order sent! tx: ${result.tx_hash ? result.tx_hash.substring(0, 16) + '...' : 'submitted'}`);
      pendingOrder = null;
      document.getElementById('order-modal').classList.remove('active');
    } catch (e) {
      App.showToast(`Error: ${e.message}`);
    } finally {
      button.disabled = false;
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
    refreshMetrics();

    document.querySelectorAll('.ohlcv-tf').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.ohlcv-tf').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        loadOHLCV(btn.dataset.tf);
      });
    });

    App.on('pool_info', updatePoolInfo);
    App.on('heat_metric', updateHeatMetrics);
    App.on('block', () => { loadOrderbook(); refreshMetrics(); });

    setInterval(loadOrderbook, 10000);
    setInterval(refreshMetrics, 10000);
  }

  return { init, showOrderPreview, executeOrder };
})();

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { ...HearthOrder, _ui: Hearth };
}
if (typeof document !== 'undefined') document.addEventListener('DOMContentLoaded', Hearth.init);
