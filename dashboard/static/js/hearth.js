// ── Hearth — the salon floor ─────────────────────────────────────────────────
'use strict';

// Chart colours are resolved from the stylesheet on every theme change, so the
// plot can never drift from the surface it is drawn on. Nothing is cached.
const CHART_TOKENS = [
  '--dir-up', '--dir-up-line', '--dir-down', '--dir-down-line',
  '--ink-30', '--ink-20', '--ink-50', '--ink-90',
  '--hairline', '--hairline-soft',
  '--accent', '--accent-line', '--flame',
  '--surface-active', '--surface-raised', '--surface-panel'
];

const ChartStyle = (() => {
  const read = () => App.tokens(CHART_TOKENS);

  // Direction carries the two directional hues and nothing else. Overlays,
  // axes and the crosshair stay in the neutral or the house accent so they
  // never compete with the data for the eye.
  const build = t => ({
    grid: {
      show: true,
      horizontal: { color: t['--hairline-soft'] },
      vertical: { color: t['--hairline-soft'] }
    },
    candle: {
      type: 'candle_solid',
      bar: {
        upColor: t['--dir-up'],
        downColor: t['--dir-down'],
        noChangeColor: t['--ink-30'],
        upBorderColor: t['--dir-up'],
        downBorderColor: t['--dir-down'],
        noChangeBorderColor: t['--ink-30'],
        upWickColor: t['--dir-up-line'],
        downWickColor: t['--dir-down-line'],
        noChangeWickColor: t['--ink-20']
      },
      areaLineSize: 1,
      priceMark: {
        high: { color: t['--ink-50'], textOffset: 5, textSize: 10 },
        low: { color: t['--ink-50'], textOffset: 5, textSize: 10 },
        last: { upColor: t['--dir-up'], downColor: t['--dir-down'], noChangeColor: t['--ink-30'] }
      }
    },
    indicator: {
      ohlc: { upColor: t['--dir-up'], downColor: t['--dir-down'], noChangeColor: t['--ink-30'] },
      // Volume takes the candle's own direction, translucent. A bar drawn
      // in an identity colour reads as a third series that means nothing.
      bars: [{ upColor: t['--dir-up'] + '33', downColor: t['--dir-down'] + '33',
               noChangeColor: t['--ink-30'] + '33',
               borderUpColor: t['--dir-up'] + '66', borderDownColor: t['--dir-down'] + '66',
               borderNoChangeColor: t['--ink-30'] + '66' }],
      lines: [
        { color: t['--accent'], size: 1 },   // MA — the house accent
        { color: t['--ink-50'], size: 1 },   // EMA — neutral
        { color: t['--flame'], size: 1 }     // VOL — the complication
      ]
    },
    xAxis: { axisLine: { color: t['--hairline'] }, tickLine: { color: t['--hairline-soft'] }, tickText: { color: t['--ink-30'], size: 10 } },
    yAxis: { axisLine: { color: t['--hairline'] }, tickLine: { color: t['--hairline-soft'] }, tickText: { color: t['--ink-30'], size: 10 } },
    separator: { color: t['--hairline'] },
    crosshair: {
      horizontal: { line: { color: t['--accent-line'], style: 1 }, text: { color: t['--ink-90'], backgroundColor: t['--surface-active'] } },
      vertical: { line: { color: t['--accent-line'], style: 1 }, text: { color: t['--ink-90'], backgroundColor: t['--surface-active'] } }
    }
  });

  return {
    build,
    read,
    // Overlays are an annotation on the chart, not a feature of it.
    overlay: () => {
      const t = read();
      const s = { line: { color: t['--accent'], style: 0, size: 1 }, text: { color: t['--ink-50'], size: 10 } };
      return {
        segment: { name: 'segment', styles: s },
        horizontalRay: { name: 'priceLine', styles: { ...s, line: { color: t['--accent'], style: 2, size: 1 } } },
        fibonacci: { name: 'fibonacciLine', styles: { ...s, polyline: { color: t['--accent-line'], style: 2, size: 1 } } },
        rectangle: { name: 'rect', styles: { ...s, polygon: { color: t['--accent'] + '18', borderColor: t['--accent-line'] } } }
      };
    }
  };
})();

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

  function isExactAtomic(value) {
    return (typeof value === 'number' && Number.isSafeInteger(value) && value > 0) ||
      (typeof value === 'string' && /^\d{1,20}$/.test(value) && BigInt(value) > 0n);
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

  return { atomic, formatAtomic, isExactAtomic, limit, market };
})();

const Hearth = (() => {
  let priceChart;
  let currentTf = '1h';
  let orderSide = 0;   // 0=buy, 1=sell
  let orderType = 'limit'; // limit, market (market uses AMM under the hood)
  let pendingOrder = null;
  let indicators = { sma20: true, ema12: false, vol: true };
  let activeDrawTool = null;
  let currentSpotPrice = null;
  let userWalletBalance = 0;
  // Which series the pool chart is currently showing. 'pool' is the live
  // HΞΔŦ-per-XFG feed; 'archive' is real XFG/USD history. The two are different
  // units, so every label and readout keys off this.
  let chartSource = 'pool';

  // klinecharts 9.8 has no removeAllOverlay(); overlays are removed by id.
  function clearOverlays(chart) {
    if (!chart) return;
    try {
      const list = chart.getCompleteOverlays ? chart.getCompleteOverlays() : [];
      (list || []).forEach(o => {
        const id = o && (o.id || o.name);
        if (id) chart.removeOverlay(id);
      });
    } catch (e) {
      console.warn('[hearth] clearing overlays failed:', e);
    }
  }

  function initPriceChart() {
    const container = document.getElementById('hearth-chart');
    if (!container || typeof klinecharts === 'undefined') return;

    priceChart = klinecharts.init(container, { styles: ChartStyle.build(ChartStyle.read()) });

    const crosshairEl = document.getElementById('chart-crosshair');
    priceChart.subscribeAction('onCrosshairChange', (event) => {
      if (!event || !event.data || !event.data.kLineData) {
        crosshairEl.classList.remove('visible');
        return;
      }
      crosshairEl.classList.add('visible');
      const d = event.data.kLineData;
      // The chart carries two different units depending on where its series came
      // from, and the readout must never imply the wrong one.
      const f = chartSource === 'archive' ? XfgArchive.usd : v => v.toFixed(5);
      const u = chartSource === 'archive' ? '$' : '';
      document.getElementById('ch-o').textContent = 'O ' + u + f(d.open);
      document.getElementById('ch-h').textContent = 'H ' + u + f(d.high);
      document.getElementById('ch-l').textContent = 'L ' + u + f(d.low);
      document.getElementById('ch-c').textContent = 'C ' + u + f(d.close);
      document.getElementById('ch-v').textContent = 'V ' + XfgArchive.vol(d.volume);
      const date = new Date(d.timestamp);
      document.getElementById('ch-time').textContent = date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' }) + ' ' + date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    });

    window.addEventListener('resize', () => {
      if (priceChart) priceChart.resize();
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
          clearOverlays(priceChart);
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

    rebuildIndicators();
  }

  function rebuildIndicators() {
    if (!priceChart) return;
    try { priceChart.removeIndicator('candle_pane', 'MA'); } catch (e) { }
    try { priceChart.removeIndicator('candle_pane', 'EMA'); } catch (e) { }
    try { priceChart.removeIndicator('vol_pane', 'VOL'); } catch (e) { }
    if (indicators.sma20) priceChart.createIndicator('MA', false, { id: 'candle_pane' });
    if (indicators.ema12) priceChart.createIndicator('EMA', false, { id: 'candle_pane' });
    if (indicators.vol) priceChart.createIndicator('VOL', false, { id: 'vol_pane' });
  }

  function startDrawing(tool) {
    if (!activeDrawTool || !priceChart) return;
    const config = ChartStyle.overlay()[tool];
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
    return plotArchive(timeframe);
  }

  function setCaption(text) {
    const el = document.getElementById('chart-caption');
    if (el) el.textContent = text;
  }

  // Real daily XFG/USD history onto the main chart. The source is daily, so 1H
  // and 4H have nothing honest to show and fall back to daily rather than
  // inventing intraday bars.
  async function plotArchive(timeframe) {
    const tf = (timeframe === '1w') ? '1w' : '1d';
    try {
      await XfgArchive.load();
    } catch (e) {
      console.warn('[hearth] price archive unavailable:', e);
      chartSource = 'archive';
      setCaption('XFG / USD — archive unavailable');
      if (priceChart) priceChart.applyNewData([]);
      return;
    }
    if (!priceChart) return;
    const s = XfgArchive.span();
    chartSource = 'archive';
    setCaption(`XFG / USD · ${tf === '1w' ? 'weekly' : 'daily'} archive · `
      + `${s.first} – ${s.last} · not the pool price`);
    priceChart.applyNewData(XfgArchive.series(tf));
  }

  // ── Depth Ladder ──

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
    if (!bidsContainer || !asksContainer) return;

    const orders = Array.isArray(data && data.orders) ? data.orders : [];
    const valid = orders.filter(o => o && (o.side === 0 || o.side === 1) &&
      HearthOrder.isExactAtomic(o.target_price) && HearthOrder.isExactAtomic(o.amount) && !o.withdrawn);
    const bids = valid.filter(o => o.side === 0)
      .sort((a, b) => Number(b.target_price) - Number(a.target_price)).slice(0, 18);
    const asks = valid.filter(o => o.side === 1)
      .sort((a, b) => Number(a.target_price) - Number(b.target_price)).slice(0, 18);

    const renderRows = (sideOrders, asset) => sideOrders.map(order => {
      const price = HearthOrder.formatAtomic(order.target_price);
      const amount = HearthOrder.formatAtomic(order.amount);
      return `<div class="ladder-row" data-price="${price}" data-amount="${amount}" data-side="${order.side}" title="Prefill ${amount} ${asset} at ${price} HEAT/XFG">
        <span class="price-val">${price}</span>
        <span class="amt-val">${amount} ${asset}</span>
      </div>`;
    }).join('');
    bidsContainer.innerHTML = renderRows(bids, 'HEAT') ||
      '<div class="hearth-empty-state">No live bids</div>';
    asksContainer.innerHTML = renderRows(asks, 'XFG') ||
      '<div class="hearth-empty-state">No live asks</div>';

    [bidsContainer, asksContainer].forEach(container => {
      container.querySelectorAll('.ladder-row').forEach(row => {
        row.addEventListener('click', () => {
          const side = Number(row.dataset.side);
          document.getElementById(side === 0 ? 'tab-buy' : 'tab-sell')?.click();
          document.querySelector('.type-chip[data-type="limit"]')?.click();
          document.getElementById('order-price').value = row.dataset.price;
          document.getElementById('order-amount').value = row.dataset.amount;
          pendingOrder = null;
          updateOrderEstimate();
          App.showToast(`Prefilled ${row.dataset.amount} ${side === 0 ? 'HEAT' : 'XFG'} at ${row.dataset.price} HEAT/XFG`);
        });
      });
    });
  }

  // ── Telemetry Strip Upgrades ──

  function setMetricText(id, value) {
    const element = document.getElementById(id);
    if (element) element.textContent = value;
  }

  function updatePoolInfo(data) {
    const valid = data && data.status === 'OK' &&
      Number(data.reserve_xfg) > 0 && Number(data.reserve_heat) > 0 &&
      Number(data.spot_price) > 0;
    currentSpotPrice = valid ? Number(data.spot_price) : null;
    setMetricText('pool-xfg', valid ? App.fmtXfg(data.reserve_xfg) + ' XFG' : '—');
    setMetricText('pool-heat', valid ? App.fmtHeat(data.reserve_heat) + ' HΞ∆Ŧ' : '—');
    setMetricText('price-xfg-heat', valid
      ? App.fmtPrice(data.spot_price) + ' HΞ∆Ŧ / XFG' : '—');
    setMetricText('price-spread', valid && Number.isSafeInteger(Number(data.height))
      ? `Live pool · height ${data.height}` : 'Live pool unavailable');
    updateOrderEstimate();
  }

  function updateHeatMetrics(data) {
    const valid = data && data.status === 'OK';
    setMetricText('heat-supply', valid ? App.fmtHeat(data.heat_supply) + ' HΞ∆Ŧ' : '—');
    setMetricText('heat-burned', valid ? App.fmtXfg(data.total_burned_xfg) + ' XFG' : '—');
    const numerator = valid ? Number(data.redemption_price_num) : 0;
    const denominator = valid ? Number(data.redemption_price_denom) : 0;
    setMetricText('heat-redemption', numerator > 0 && denominator > 0
      ? (numerator / denominator).toFixed(4) + ' XFG / HΞ∆Ŧ' : '—');
  }

  function updateReferencePrice(data) {
    const valid = data && data.status === 'OK';
    const peg = valid ? Number(data.heat_peg_usd) : 0;
    const impliedXfgUsd = valid ? Number(data.xfg_spot_usd) : 0;
    setMetricText('heat-peg', Number.isFinite(peg) && peg > 0
      ? `1 HΞ∆Ŧ ≋ $${peg.toFixed(4)}` : '—');
    setMetricText('price-xfg-usd', Number.isFinite(impliedXfgUsd) && impliedXfgUsd > 0
      ? `Reference-implied $${impliedXfgUsd.toFixed(4)}` : 'Reference-implied USD unavailable');
  }

  async function refreshMetrics() {
    const [pool, heat, price] = await Promise.allSettled([
      App.daemonPost('/amm_pool_info'), App.daemonPost('/heat_metrics'),
      App.daemonPost('/get_fuego_price')
    ]);
    updatePoolInfo(pool.status === 'fulfilled' ? pool.value : null);
    updateHeatMetrics(heat.status === 'fulfilled' ? heat.value : null);
    updateReferencePrice(price.status === 'fulfilled' ? price.value : null);
  }

  // ── Order Console ──

  function initOrderForm() {
    const tabBuy = document.getElementById('tab-buy');
    const tabSell = document.getElementById('tab-sell');
    if (tabBuy && tabSell) {
      tabBuy.addEventListener('click', () => {
        orderSide = 0;
        tabBuy.classList.add('active');
        tabSell.classList.remove('active');
        updateOrderButton();
        updateOrderEstimate();
      });
      tabSell.addEventListener('click', () => {
        orderSide = 1;
        tabSell.classList.add('active');
        tabBuy.classList.remove('active');
        updateOrderButton();
        updateOrderEstimate();
      });
    }

    document.querySelectorAll('.type-chip').forEach(chip => {
      chip.addEventListener('click', () => {
        document.querySelectorAll('.type-chip').forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        orderType = chip.dataset.type;
        updateOrderFields();
        updateOrderButton();
        updateOrderEstimate();
      });
    });

    document.querySelectorAll('.pct-chip').forEach(chip => {
      chip.addEventListener('click', () => {
        const percent = Number(chip.dataset.pct) * 100;
        if (orderSide === 0) {
          App.showToast('HEAT balance unavailable; enter an exact HEAT budget');
          return;
        }
        if (!Number.isSafeInteger(userWalletBalance) || userWalletBalance <= 0) {
          App.showToast('XFG balance unavailable');
          return;
        }
        if (!Number.isInteger(percent) || percent <= 0 || percent >= 100) {
          App.showToast('Enter an exact amount and leave XFG for the network fee');
          return;
        }
        const amount = BigInt(userWalletBalance) * BigInt(percent) / 100n;
        document.getElementById('order-amount').value = HearthOrder.formatAtomic(amount);
        pendingOrder = null;
        updateOrderEstimate();
      });
    });

    const amtInput = document.getElementById('order-amount');
    const priceInput = document.getElementById('order-price');
    if (amtInput) amtInput.addEventListener('input', () => { pendingOrder = null; updateOrderEstimate(); });
    if (priceInput) priceInput.addEventListener('input', () => { pendingOrder = null; updateOrderEstimate(); });
    const expiryInput = document.getElementById('order-expiry');
    if (expiryInput) expiryInput.addEventListener('input', () => { pendingOrder = null; updateOrderEstimate(); });

    const previewBtn = document.getElementById('order-preview-btn');
    if (previewBtn) previewBtn.addEventListener('click', showOrderPreview);

    const closeBtn = document.getElementById('order-modal-close');
    if (closeBtn) closeBtn.addEventListener('click', () => {
      pendingOrder = null;
      document.getElementById('order-modal').classList.remove('active');
    });

    const execBtn = document.getElementById('order-exec-btn');
    if (execBtn) execBtn.addEventListener('click', executeOrder);

    const copyBtn = document.getElementById('order-copy-btn');
    if (copyBtn) copyBtn.addEventListener('click', () => {
      App.copyToClipboard(document.getElementById('order-cli-cmd').textContent);
    });

    updateOrderFields();
    updateOrderButton();
  }

  function updateOrderFields() {
    const priceGroup = document.getElementById('price-group');
    const expiryGroup = document.getElementById('expiry-group');
    if (orderType === 'market') {
      if (priceGroup) priceGroup.style.display = 'none';
      if (expiryGroup) expiryGroup.style.display = 'none';
    } else {
      if (priceGroup) priceGroup.style.display = '';
      if (expiryGroup) expiryGroup.style.display = '';
    }
    const amountLabel = document.getElementById('order-amount-label');
    if (amountLabel) amountLabel.textContent = orderSide === 0
      ? 'HEAT budget' : 'XFG amount';
  }

  function updateOrderButton() {
    const btn = document.getElementById('order-preview-btn');
    if (!btn) return;
    const side = orderSide === 0 ? 'Acquire' : 'Release';
    btn.textContent = `Review ${side} XFG`;
    btn.className = `btn btn-block ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
    updateOrderFields();
  }

  function updateOrderEstimate() {
    const sub = document.getElementById('order-estimate-sub');
    if (!sub) return;
    sub.textContent = orderType === 'market'
      ? 'Exact pool output is shown in a live quote before submission'
      : 'A limit order is submitted on-chain; fill amount and timing are not guaranteed';
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
      pendingOrder = Object.freeze({ ...plan, params: Object.freeze({ ...plan.params }) });
      const sideLabel = plan.side === 0 ? 'Acquire XFG' : 'Release XFG';
      const field = (label, value) =>
        `<div><span class="rf-label">${label}</span><span class="rf-value">${value}</span></div>`;
      let html = `<div class="rf"><div class="rf-head"><span class="rf-ref">HEARTH · ORDER REVIEW</span>`;
      html += `<span class="badge ${plan.side === 0 ? 'badge-green' : 'badge-red'}">${sideLabel}</span></div><div class="rf-grid">`;
      html += field('Manner', plan.type === 'limit' ? 'Standing limit' : 'Immediate pool swap');
      html += field('Input', `${HearthOrder.formatAtomic(plan.amount)} ${plan.inputAsset}`);
      if (plan.type === 'limit') {
        html += field('Limit', `${HearthOrder.formatAtomic(plan.params.target_price)} HEAT/XFG`);
        html += field('Expiry height', plan.params.expiration || 'None');
      } else {
        html += field('Fixed output', `${HearthOrder.formatAtomic(plan.expected)} ${plan.outputAsset}`);
      }
      html += '</div></div>';
      if (plan.type === 'market') {
        html += '<p class="rf-note">The quote is checked again before submission. A worse pool quote requires a new review; a later block can still reject the transaction.</p>';
      } else {
        html += '<p class="rf-note">This creates a standing on-chain order. Execution depends on available liquidity and the selected limit.</p>';
      }
      document.getElementById('order-modal-details').innerHTML = html;
      const request = document.getElementById('order-cli-cmd');
      request.textContent = JSON.stringify({
        method: plan.type === 'limit' ? 'place_limit_order' : 'amm_swap',
        params: plan.params
      }, null, 2);
      request.hidden = false;
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
      if (!result || !/^[a-fA-F0-9]{64}$/.test(result.tx_hash || '')) {
        throw new Error('Wallet did not return a transaction hash; check wallet status before retrying');
      }
      App.showToast(`Order sent · ${result.tx_hash.substring(0, 16)}…`);
      pendingOrder = null;
      document.getElementById('order-modal').classList.remove('active');
      loadOrderbook();
    } catch (e) {
      App.showToast(`Order submission: ${e.message || 'Wallet unavailable'}`);
    } finally {
      button.disabled = false;
    }
  }

  async function loadUserBalance() {
    try {
      const balance = await App.walletRpc('getbalance');
      const available = Number(balance && balance.available_balance);
      userWalletBalance = Number.isSafeInteger(available) && available > 0 ? available : 0;
    } catch (e) {
      userWalletBalance = 0;
    }
  }

  // ── Lifecycle ──

  function init() {
    initPriceChart();
    initOrderForm();

    loadOHLCV(currentTf);
    loadOrderbook();
    updatePoolInfo(null);
    updateHeatMetrics(null);
    loadUserBalance();
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
    App.on('block', () => { loadOrderbook(); refreshMetrics(); loadUserBalance(); });

    App.on('theme', () => {
      if (priceChart) {
        try { priceChart.setStyles(ChartStyle.build(ChartStyle.read())); }
        catch (e) { console.warn('[hearth] theme repaint failed:', e); }
      }
    });

    setInterval(loadOrderbook, 10000);
    setInterval(refreshMetrics, 10000);
  }

  return { init, showOrderPreview, executeOrder };
})();

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { ...HearthOrder, _ui: Hearth };
}
if (typeof document !== 'undefined') document.addEventListener('DOMContentLoaded', Hearth.init);
