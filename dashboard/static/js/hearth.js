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

const Hearth = (() => {
  let priceChart;
  let currentTf = '1h';
  let orderSide = 0;   // 0 = Buy XFG (bid), 1 = Sell XFG (ask)
  let orderType = 'limit'; // limit, market
  let indicators = { sma20: true, ema12: false, vol: true };
  let activeDrawTool = null;
  let currentSpotPrice = 1580000;
  let userWalletBalance = 0;
  // Which series the pool chart is currently showing. 'pool' is the live
  // HΞΔŦ-per-XFG feed; 'archive' is real XFG/USD history. The two are different
  // units, so every label and readout keys off this.
  let chartSource = 'pool';

  // Depth still falls back to a placeholder ladder while the house daemons are
  // booting, but the price chart no longer invents candles: it shows the real
  // archive below, and says plainly when it has no live feed.

  function mockOrderbook() {
    const bestBid = 1575000;
    const bestAsk = 1585000;
    const bids = [], bidAmts = [], bidDepths = [];
    const asks = [], askAmts = [], askDepths = [];
    let cumBid = 0, cumAsk = 0;
    for (let i = 0; i < 16; i++) {
      bids.push(bestBid - i * 14000);
      const amt = Math.floor(Math.random() * 750 + 220) * 10000000;
      bidAmts.push(amt);
      cumBid += amt;
      bidDepths.push(cumBid);

      asks.push(bestAsk + i * 14000);
      const aamt = Math.floor(Math.random() * 650 + 180) * 10000000;
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
    spot_price: 1580000,
    reserve_xfg: 125000 * 10000000,
    reserve_heat: 19750000 * 10000000,
    total_lp_shares: 42000,
    accumulated_lp_fees: 3200 * 10000000,
    epoch_swap_fees: 180 * 10000000
  };

  const MOCK_HEAT = {
    heat_supply: 8500000 * 10000000,
    redemption_price: 1580000,
    xfg_burned: 1200000 * 10000000,
    fee_pool: 45000 * 10000000
  };

  // ── Chart ─────────────────────────────────────────────────────────────────
  // Direction carries the only two chromatic hues. Overlays, axes and the
  // crosshair are drawn in neutral or the house accent so they never compete
  // with the data for the eye.

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

  async function loadOHLCV(timeframe) {
    currentTf = timeframe;
    let data = [];
    try {
      const candles = await App.rpc('get_ohlvc', { timeframe, count: 200 });
      if (candles && candles.candles && candles.candles.length) {
        data = candles.candles.map(c => ({
          timestamp: c.t * 1000,
          open: c.o / App.COIN,
          high: c.h / App.COIN,
          low: c.l / App.COIN,
          close: c.c / App.COIN,
          volume: c.v / App.COIN
        }));
      }
    } catch (e) {
      // offline fallback
    }

    if (data.length === 0) {
      // No live pool feed. This used to draw random mock candles, which put
      // invented prices on a trading chart indistinguishable from real ones.
      // It now plots the real XFG/USD archive instead, and says in the caption
      // that the unit has changed — the pool's own HΞΔŦ quote stays in the spot
      // bar above and is never mixed into this axis.
      return plotArchive(timeframe);
    }

    chartSource = 'pool';
    setCaption('Live pool · HΞΔŦ per XFG');
    if (priceChart) {
      priceChart.applyNewData(data);
    }
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
      const data = await App.rpc('get_orderbook_state', { depth: 20 });
      if (data && (data.bid_prices || data.ask_prices)) {
        renderHearthOrderbook(data);
        return;
      }
    } catch (e) {
      // offline fallback
    }
    renderHearthOrderbook(mockOrderbook());
  }

  function renderHearthOrderbook(data) {
    const bidsContainer = document.getElementById('hearth-bids');
    const asksContainer = document.getElementById('hearth-asks');
    if (!bidsContainer || !asksContainer) return;

    const bids = data.bid_prices || [];
    const asks = data.ask_prices || [];
    const bidAmounts = data.bid_amounts || data.bid_depths || [];
    const bidDepths = data.bid_depths || [];
    const askAmounts = data.ask_amounts || data.ask_depths || [];
    const askDepths = data.ask_depths || [];

    const maxBidDepth = Math.max(...bidDepths, 1);
    const maxAskDepth = Math.max(...askDepths, 1);

    // Bids Ladder (Gold/Firegold)
    let bidHtml = '';
    for (let i = 0; i < bids.length; i++) {
      const pct = Math.min(100, Math.round(((bidDepths[i] || 0) / maxBidDepth) * 100));
      const pWhole = (bids[i] / App.COIN).toFixed(5);
      const aWhole = (bidAmounts[i] / App.COIN).toFixed(2);
      bidHtml += `
        <div class="ladder-row" data-price="${pWhole}" data-amount="${aWhole}" title="Prefill ${pWhole} HΞ∆Ŧ">
          <span class="price-val">${pWhole}</span>
          <span class="amt-val">${aWhole} XFG</span>
          <div class="row-bar" style="width:${pct}%"></div>
        </div>`;
    }
    bidsContainer.innerHTML = bidHtml || '<div class="hearth-empty-state">No bids</div>';

    // Asks Ladder (White-Hot Blue)
    let askHtml = '';
    for (let i = asks.length - 1; i >= 0; i--) {
      const pct = Math.min(100, Math.round(((askDepths[i] || 0) / maxAskDepth) * 100));
      const pWhole = (asks[i] / App.COIN).toFixed(5);
      const aWhole = (askAmounts[i] / App.COIN).toFixed(2);
      askHtml += `
        <div class="ladder-row" data-price="${pWhole}" data-amount="${aWhole}" title="Prefill ${pWhole} HΞ∆Ŧ">
          <span class="price-val">${pWhole}</span>
          <span class="amt-val">${aWhole} XFG</span>
          <div class="row-bar" style="width:${pct}%"></div>
        </div>`;
    }
    asksContainer.innerHTML = askHtml || '<div class="hearth-empty-state">No asks</div>';

    // Instant click prefill
    [bidsContainer, asksContainer].forEach(container => {
      container.querySelectorAll('.ladder-row').forEach(row => {
        row.addEventListener('click', () => {
          const p = row.dataset.price;
          const a = row.dataset.amount;
          if (p && document.getElementById('order-price')) {
            document.getElementById('order-price').value = p;
          }
          if (a && document.getElementById('order-amount')) {
            document.getElementById('order-amount').value = a;
          }
          updateOrderEstimate();
          App.showToast(`Prefilled ${a} XFG @ ${p} HΞ∆Ŧ`);
        });
      });
    });
  }

  // ── Telemetry Strip Upgrades ──

  function updatePoolInfo(data) {
    const d = data || MOCK_POOL;
    currentSpotPrice = d.spot_price || 1580000;

    const xfgReserve = document.getElementById('pool-xfg');
    const heatReserve = document.getElementById('pool-heat');
    if (xfgReserve) xfgReserve.textContent = App.fmtXfg(d.reserve_xfg) + ' XFG';
    if (heatReserve) heatReserve.textContent = App.fmtHeat(d.reserve_heat) + ' HΞ∆Ŧ';

    const priceEl = document.getElementById('price-xfg-heat');
    const usdEl = document.getElementById('price-xfg-usd');
    if (priceEl) {
      const p = currentSpotPrice / App.COIN;
      priceEl.textContent = p.toFixed(5) + ' HΞ∆Ŧ';
      const usd = p * 1.58;
      if (usdEl) usdEl.textContent = `≈ $${usd.toFixed(4)} USD`;
    }

    updateOrderEstimate();
  }

  function updateHeatMetrics(data) {
    const d = data || MOCK_HEAT;
    const supplyEl = document.getElementById('heat-supply');
    const burnedEl = document.getElementById('heat-burned');
    const redemptionEl = document.getElementById('heat-redemption');

    if (supplyEl) supplyEl.textContent = App.fmtHeat(d.heat_supply || d.total_supply) + ' HΞ∆Ŧ';
    if (burnedEl) burnedEl.textContent = App.fmtXfg(d.xfg_burned || d.total_burned) + ' XFG';

    const redemptionPrice = d.redemption_price || 1580000;
    const xfgPerHeat = redemptionPrice / App.COIN;
    if (redemptionEl) redemptionEl.textContent = `${xfgPerHeat.toFixed(2)} XFG / HΞ∆Ŧ`;
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
        const pct = parseFloat(chip.dataset.pct) || 0;
        let base = userWalletBalance > 0 ? (userWalletBalance / App.COIN) : 100;
        const targetAmt = (base * pct).toFixed(2);
        document.getElementById('order-amount').value = targetAmt;
        updateOrderEstimate();
      });
    });

    const amtInput = document.getElementById('order-amount');
    const priceInput = document.getElementById('order-price');
    if (amtInput) amtInput.addEventListener('input', updateOrderEstimate);
    if (priceInput) priceInput.addEventListener('input', updateOrderEstimate);

    const previewBtn = document.getElementById('order-preview-btn');
    if (previewBtn) previewBtn.addEventListener('click', showOrderPreview);

    const closeBtn = document.getElementById('order-modal-close');
    if (closeBtn) closeBtn.addEventListener('click', () => {
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
  }

  function updateOrderButton() {
    const btn = document.getElementById('order-preview-btn');
    if (!btn) return;
    const actionWord = orderSide === 0 ? 'Acquire' : 'Release';
    const modeWord = orderType === 'limit' ? 'Standing Limit' : 'Immediate';
    btn.textContent = `${actionWord} · ${modeWord}`;
    btn.className = `btn btn-block ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
  }

  function updateOrderEstimate() {
    const amt = parseFloat(document.getElementById('order-amount')?.value) || 0;
    const sub = document.getElementById('order-estimate-sub');
    if (!sub) return;

    if (amt <= 0) {
      sub.textContent = 'Continuous depth against the salon pool';
      return;
    }

    let price = currentSpotPrice / App.COIN;
    if (orderType === 'limit') {
      const customPrice = parseFloat(document.getElementById('order-price')?.value);
      if (customPrice > 0) price = customPrice;
    }

    const grossProceeds = amt * price;
    const fee = grossProceeds * 0.01; // 1% taker fee
    const cdYieldShare = fee * 0.70;  // 70% to CD APY pool
    const netProceeds = orderSide === 0 ? grossProceeds + fee : grossProceeds - fee;

    sub.innerHTML = orderSide === 0
      ? `Consideration <strong style="color:var(--ink-100);">${netProceeds.toFixed(4)} HΞ∆Ŧ</strong> · salon fee 1% · 70% to CD yield`
      : `Proceeds <strong style="color:var(--ink-100);">${netProceeds.toFixed(4)} HΞ∆Ŧ</strong> · net of salon fee 1% · 70% to CD yield`;
  }

  function showOrderPreview() {
    const amount = document.getElementById('order-amount')?.value;
    if (!amount || parseFloat(amount) <= 0) {
      App.showToast('Enter an amount in XFG');
      return;
    }

    const side = orderSide === 0 ? 'buy' : 'sell';
    const sideLabel = orderSide === 0 ? 'Acquire XFG' : 'Release XFG';
    const details = document.getElementById('order-modal-details');

    let price = (currentSpotPrice / App.COIN).toFixed(5);
    let expiry = '4320';
    if (orderType === 'limit') {
      const pInput = document.getElementById('order-price')?.value;
      if (!pInput || parseFloat(pInput) <= 0) {
        App.showToast('Enter a limit price');
        return;
      }
      price = parseFloat(pInput).toFixed(5);
      expiry = document.getElementById('order-expiry')?.value || '4320';
    }

    const gross = (parseFloat(amount) * parseFloat(price)).toFixed(4);
    const cdYield = (gross * 0.01 * 0.70).toFixed(4);

    const field = (label, value, tone) =>
      `<div><span class="rf-label">${label}</span><span class="rf-value"${tone ? ` style="color:var(${tone})"` : ''}>${value}</span></div>`;

    details.innerHTML = `
      <div class="rf">
        <div class="rf-head">
          <span class="rf-ref">HEARTH · ${Date.now().toString(36).toUpperCase()}</span>
          <span class="badge ${orderSide === 0 ? 'badge-green' : 'badge-red'}">${sideLabel}</span>
        </div>
        <div class="rf-grid">
          ${field('Manner', orderType === 'limit' ? 'Standing Limit' : 'Immediate (Pool)')}
          ${field('Amount', `${amount} XFG`, '--maison-bright')}
          ${field('Limit', `${price} HΞ∆Ŧ`, '--heat-bright')}
          ${field('Consideration', `≈ ${gross} HΞ∆Ŧ`)}
          ${field('Salon Fee 1%', `70% CD yield · ${cdYield} HΞ∆Ŧ`)}
          ${field('Term', `${expiry} blocks · ~${(parseInt(expiry) / 720).toFixed(1)} epochs`)}
        </div>
      </div>
      <p class="rf-note">
        Standing orders are committed on-chain and matched at each block against
        the continuous salon pool at its prevailing mid.
      </p>`;

    const atomicAmt = Math.round(parseFloat(amount) * App.COIN);
    const cliEl = document.getElementById('order-cli-cmd');
    if (orderType === 'limit') {
      cliEl.textContent = `fire_wallet place_order ${side} ${amount} ${price} ${expiry}`;
    } else {
      cliEl.textContent = `fire_wallet amm_swap ${orderSide === 0 ? 0 : 1} ${atomicAmt}`;
    }
    cliEl.style.display = 'block';

    document.getElementById('order-modal').classList.add('active');
  }

  async function executeOrder() {
    const amount = document.getElementById('order-amount')?.value;
    if (!amount) return;

    try {
      const atomicAmt = Math.round(parseFloat(amount) * App.COIN);
      let result;

      if (orderType === 'limit') {
        const price = document.getElementById('order-price')?.value;
        const expiry = parseInt(document.getElementById('order-expiry')?.value) || 4320;
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

      App.showToast(`Order sent! Tx: ${result.tx_hash ? result.tx_hash.substring(0, 16) + '…' : 'ok'}`);
      document.getElementById('order-modal').classList.remove('active');
      loadOrderbook();
    } catch (e) {
      App.showToast(`Order submission: ${e.message || 'Check wallet daemon'}`);
    }
  }

  async function loadUserBalance() {
    try {
      const bal = await App.walletRpc('getbalance');
      if (bal && bal.availableBalance != null) {
        userWalletBalance = bal.availableBalance;
      }
    } catch (e) {
      // offline
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

    document.querySelectorAll('.ohlcv-tf').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.ohlcv-tf').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        loadOHLCV(btn.dataset.tf);
      });
    });

    App.on('pool_info', updatePoolInfo);
    App.on('heat_metric', updateHeatMetrics);
    App.on('block', () => {
      loadOrderbook();
      loadOHLCV(currentTf);
    });

    // A register change repaints the plot in place — no reload, no re-fetch,
    // and the selected indicators and overlays are left exactly as they were.
    App.on('theme', () => {
      if (priceChart) {
        try { priceChart.setStyles(ChartStyle.build(ChartStyle.read())); }
        catch (e) { console.warn('[hearth] theme repaint failed:', e); }
      }
    });

    setInterval(loadOrderbook, 8000);  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', Hearth.init);
