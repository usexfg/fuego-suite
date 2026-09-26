// ── Hearth Exchange — Monaco Terminal Logic ──────────────────────────────────
'use strict';

const Hearth = (() => {
  let priceChart;
  let currentTf = '1h';
  let orderSide = 0;   // 0 = Buy XFG (bid), 1 = Sell XFG (ask)
  let orderType = 'limit'; // limit, market
  let indicators = { sma20: true, ema12: false, vol: true };
  let activeDrawTool = null;
  let currentSpotPrice = 1580000;
  let userWalletBalance = 0;

  // ──  Mock Data (Fallback whe
n daemons are booting / syncing) ──
  function mockCandles(count) {
    const now = Math.floor(Date.now() / 1000);
    const candles = [];
    let price = 1580000;
    for (let i = count; i > 0; i--) {
      const t = now - i * 3600;
      const change = (Math.random() - 0.48) * 35000;
      const open = price;
      const close = Math.max(1200000, price + change);
      const high = Math.max(open, close) + Math.random() * 18000;
      const low = Math.min(open, close) - Math.random() * 18000;
      const vol = Math.floor(Math.random() * 4500 * 10000000) + 400000000;
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

  // ── Monaco Terminal Chart (Gold / Firegold up, White-Hot Blue down) ──

  function initPriceChart() {
    const container = document.getElementById('hearth-chart');
    if (!container || typeof klinecharts === 'undefined') return;

    priceChart = klinecharts.init(container, {
      styles: {
        grid: {
          show: true,
          horizontal: { color: 'rgba(255,145,0,0.06)' },
          vertical: { color: 'rgba(255,145,0,0.04)' }
        },
        candle: {
          type: 'candle_solid',
          bar: {
            upColor: '#ff9100',          // Firegold for Up Candles
            downColor: '#00f0ff',        // White-Hot Blue for Down Candles
            noChangeColor: '#5d6175',
            upBorderColor: '#c9a44c',
            downBorderColor: '#00f0ff',
            noChangeBorderColor: '#5d6175',
            upWickColor: '#c9a44c',
            downWickColor: '#e0faff',
            noChangeWickColor: '#5d6175'
          },
          areaLineSize: 1,
          priceMark: {
            high: { color: '#c9a44c', textOffset: 5, textSize: 10 },
            low: { color: '#00f0ff', textOffset: 5, textSize: 10 },
            last: { upColor: '#ff9100', downColor: '#00f0ff', noChangeColor: '#5d6175' }
          }
        },
        indicator: {
          ohlc: { upColor: '#ff9100', downColor: '#00f0ff', noChangeColor: '#5d6175' },
          bars: [
            { color: 'rgba(0,240,255,0.45)', borderColor: 'rgba(0,240,255,0.7)' }
          ],
          lines: [
            { color: '#c9a44c', size: 1.5 }, // Gold SMA 20
            { color: '#ff9100', size: 1.5 }, // Firegold EMA 12
            { color: '#00f0ff', size: 1.5 }  // White-Hot Blue VOL
          ]
        },
        xAxis: {
          axisLine: { color: 'rgba(255,255,255,0.1)' },
          tickLine: { color: 'rgba(255,255,255,0.1)' },
          tickText: { color: '#5d6175', size: 10 }
        },
        yAxis: {
          axisLine: { color: 'rgba(255,255,255,0.1)' },
          tickLine: { color: 'rgba(255,255,255,0.1)' },
          tickText: { color: '#5d6175', size: 10 }
        },
        separator: { color: 'rgba(255,145,0,0.2)' },
        crosshair: {
          horizontal: { line: { color: '#ff9100', style: 2 }, text: { color: '#000', backgroundColor: '#c9a44c' } },
          vertical: { line: { color: '#ff9100', style: 2 }, text: { color: '#000', backgroundColor: '#c9a44c' } }
        }
      }
    });

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
    const s = { line: { color: '#ff9100', style: 0, size: 1.5 }, text: { color: '#c9a44c', size: 10 } };
    const configs = {
      segment: { name: 'segment', styles: s },
      horizontalRay: { name: 'priceLine', styles: { ...s, line: { color: '#ff9100', style: 2, size: 1.5 } } },
      fibonacci: { name: 'fibonacciLine', styles: { ...s, polyline: { color: '#c9a44c', style: 2, size: 1 } } },
      rectangle: { name: 'rect', styles: { ...s, polygon: { color: 'rgba(255,145,0,0.15)', borderColor: '#ff9100' } } }
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

    if (priceChart) {
      priceChart.applyNewData(data);
    }
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
    const actionWord = orderSide === 0 ? 'Buy' : 'Sell';
    const modeWord = orderType === 'limit' ? 'Limit Order' : 'Market Swap';
    btn.textContent = `${actionWord} XFG (${modeWord})`;
    btn.className = `btn btn-block ${orderSide === 0 ? 'btn-buy' : 'btn-sell'}`;
  }

  function updateOrderEstimate() {
    const amt = parseFloat(document.getElementById('order-amount')?.value) || 0;
    const sub = document.getElementById('order-estimate-sub');
    if (!sub) return;

    if (amt <= 0) {
      sub.textContent = 'Continuous liquidity via on-chain AMM';
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
      ? `Est. Cost: <strong style="color:var(--firegold-bright);">${netProceeds.toFixed(4)} HΞ∆Ŧ</strong> (1% fee · 70% to CD Yield)`
      : `Est. Proceeds: <strong style="color:var(--heat-blue-hot);">${netProceeds.toFixed(4)} HΞ∆Ŧ</strong> (net of 1% fee · 70% to CD Yield)`;
  }

  function showOrderPreview() {
    const amount = document.getElementById('order-amount')?.value;
    if (!amount || parseFloat(amount) <= 0) {
      App.showToast('Enter an amount in XFG');
      return;
    }

    const side = orderSide === 0 ? 'buy' : 'sell';
    const sideLabel = orderSide === 0 ? 'BUY XFG' : 'SELL XFG';
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

    let html = `
      <div style="background:#000;border:1px solid var(--border-firegold);border-radius:6px;padding:14px;margin-bottom:12px;">
        <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
          <span style="font-family:var(--font-mono);font-size:11px;color:var(--firegold-bright);">ORDER // HEARTH-${Date.now().toString(36).toUpperCase()}</span>
          <span class="badge ${orderSide === 0 ? 'badge-green' : 'badge-red'}">${sideLabel}</span>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px 14px;">
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">TYPE</span><span style="font-family:var(--font-mono);font-weight:700;">${orderType.toUpperCase()}</span></div>
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">AMOUNT</span><span style="font-family:var(--font-mono);font-weight:700;color:var(--gold-bright);">${amount} XFG</span></div>
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">PRICE</span><span style="font-family:var(--font-mono);font-weight:700;color:var(--heat-blue-hot);">${price} HΞ∆Ŧ</span></div>
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">CONSIDERATION</span><span style="font-family:var(--font-mono);font-weight:700;">≈ ${gross} HΞ∆Ŧ</span></div>
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">TAKER FEE (1%)</span><span style="font-family:var(--font-mono);color:var(--text-secondary);">70% CD Yield (${cdYield} HΞ∆Ŧ)</span></div>
          <div><span style="font-size:9px;color:var(--text-muted);display:block;">EXPIRATION</span><span style="font-family:var(--font-mono);color:var(--text-secondary);">${expiry} Blocks (~${(parseInt(expiry) / 720).toFixed(1)} Epochs)</span></div>
        </div>
      </div>
      <div style="font-size:10px;color:var(--text-muted);line-height:1.4;">
        Orders are committed on-chain via transaction extra data and matched per block against the continuous AMM pool at spot price.
      </div>`;

    details.innerHTML = html;

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

    setInterval(loadOrderbook, 8000);
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', Hearth.init);
