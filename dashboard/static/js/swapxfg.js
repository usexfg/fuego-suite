// ── DeXFG Ordergraph + Orderbook ─────────────────────────────────────────────
'use strict';

const SwapXFG = (() => {
  let oracleChart, oracleLineSeries;
  let swapDirection = 0; // 0 = XFG→CTR, 1 = CTR→XFG
  let offers = [];
  let selectedOfferId = null;
  let chainHeight = 0;
  let oraclePrices = {}; // chainKey -> { price, spread } or rate

  // SwapPair enum order (src/SwapDaemon/SwapTypes.h)
  const PAIR_BY_INDEX = [
    'SOL', 'ETH', 'XMR', 'BCH', 'ARB', 'BASE', 'KMD_SPV', 'BNB', 'DCR', 'BTC',
    'LTC', 'POLYGON', 'GLEEC', 'ROBINHOOD', 'AVAX', 'CRO', 'BOB', 'SIA',
    'UNICHAIN', 'PLASMA', 'DOGE', 'DASH', 'ZEC', 'PULSEX', 'ZANO', 'TON',
    'MONAD', 'OPTIMISM'
  ];

  const CHAIN_INFO = {
    BTC:      { icon: '/coin-icons/btc.png', color: '#f7931a', ticker: 'BTC', name: 'Bitcoin' },
    ETH:      { icon: '/coin-icons/eth.png', color: '#627eea', ticker: 'ETH', name: 'Ethereum' },
    SOL:      { icon: '/coin-icons/sol.png', color: '#9945ff', ticker: 'SOL', name: 'Solana' },
    XMR:      { icon: '/coin-icons/monero.png', color: '#ff6600', ticker: 'XMR', name: 'Monero' },
    LTC:      { icon: '/coin-icons/ltc.png', color: '#bfbbbb', ticker: 'LTC', name: 'Litecoin' },
    BCH:      { icon: '/coin-icons/bch.png', color: '#8dc351', ticker: 'BCH', name: 'Bitcoin Cash' },
    ARB:      { icon: '/coin-icons/arb.png', color: '#28a0f0', ticker: 'ARB', name: 'Arbitrum' },
    BASE:     { icon: '/coin-icons/base.png', color: '#0052ff', ticker: 'BASE', name: 'Base' },
    BNB:      { icon: '/coin-icons/bnb.png', color: '#f3ba2f', ticker: 'BNB', name: 'BNB Chain' },
    DCR:      { icon: '/coin-icons/dcr.png', color: '#2970ff', ticker: 'DCR', name: 'Decred' },
    KMD_SPV:  { icon: '/coin-icons/kmd.png', color: '#2b6def', ticker: 'KMD', name: 'Komodo' },
    POLYGON:  { icon: '/coin-icons/matic.png', color: '#8247e5', ticker: 'MATIC', name: 'Polygon' },
    GLEEC:    { icon: '/coin-icons/gleec.png', color: '#4CAF50', ticker: 'GLEEC', name: 'Gleec' },
    ROBINHOOD:{ icon: '/coin-icons/rhc.png', color: '#3A2E8C', ticker: 'ETH', name: 'Robinhood Chain' },
    AVAX:     { icon: '/coin-icons/avax.png', color: '#E84142', ticker: 'AVAX', name: 'Avalanche' },
    CRO:      { icon: '/coin-icons/cronos.png', color: '#002D74', ticker: 'CRO', name: 'Cronos' },
    BOB:      { icon: '/coin-icons/bob.png', color: '#2D9CDB', ticker: 'ETH', name: 'Bob' },
    SIA:      { icon: '/coin-icons/sc.png', color: '#00b8d4', ticker: 'SC', name: 'Sia' },
    TON:      { icon: '/coin-icons/ton.png', color: '#0098EA', ticker: 'TON', name: 'TON' },
    UNICHAIN: { icon: '/coin-icons/uni.png', color: '#FC72FF', ticker: 'ETH', name: 'Unichain' },
    PLASMA:   { icon: '/coin-icons/xpl.png', color: '#7B2FF2', ticker: 'XPL', name: 'Plasma' },
    DOGE:     { icon: '/coin-icons/doge.png', color: '#C2A633', ticker: 'DOGE', name: 'Dogecoin' },
    DASH:     { icon: '/coin-icons/dash.png', color: '#008CE7', ticker: 'DASH', name: 'Dash' },
    ZEC:      { icon: '/coin-icons/zec.png', color: '#F4B728', ticker: 'ZEC', name: 'Zcash' },
    PULSEX:   { icon: '/coin-icons/plsx.png', color: '#FF7B00', ticker: 'PLS', name: 'PulseChain' },
    ZANO:     { icon: '/coin-icons/zano.png', color: '#8A2BE2', ticker: 'ZANO', name: 'Zano' },
    MONAD:    { icon: '/coin-icons/monad.png', color: '#836EF9', ticker: 'MON', name: 'Monad' },
    OPTIMISM: { icon: '/coin-icons/op.jpg', color: '#FF0420', ticker: 'ETH', name: 'Optimism' }
  };

  const FUEGO_ICON = '/coin-icons/fuego.png';

  /** Stable alphabetical columns by display name */
  function orderedChains() {
    return Object.keys(CHAIN_INFO).sort((a, b) =>
      CHAIN_INFO[a].name.localeCompare(CHAIN_INFO[b].name)
    );
  }

  function pairKeyFromIndex(idx) {
    if (typeof idx === 'string' && CHAIN_INFO[idx]) return idx;
    const n = Number(idx);
    if (!Number.isNaN(n) && PAIR_BY_INDEX[n]) return PAIR_BY_INDEX[n];
    return null;
  }

  function normalizeOffer(raw) {
    const pairKey = pairKeyFromIndex(raw.pair);
    const xfgAmount = Number(raw.xfgAmount || raw.xfg_amount || 0);
    const filledAmount = Number(raw.filledAmount || raw.filled_amount || 0);
    const remaining = Math.max(0, xfgAmount - filledAmount);
    const rateNum = Number(raw.rateNum || raw.rate_num || 0);
    // rateNum: XFG per 1 CTR whole unit, scaled by 1e7
    const rateXfgPerCtr = rateNum / App.COIN;
    const isSell = raw.isSell !== false && raw.is_sell !== false; // default sell XFG
    const postedHeight = Number(raw.postedHeight || raw.posted_height || 0);
    const ttlBlocks = Number(raw.ttlBlocks || raw.ttl_blocks || 0);
    const expireHeight = postedHeight && ttlBlocks ? postedHeight + ttlBlocks : 0;
    const blocksLeft = expireHeight && chainHeight
      ? Math.max(0, expireHeight - chainHeight)
      : (ttlBlocks || null);

    return {
      offerId: raw.offerId || raw.offer_id || '',
      pairKey,
      pair: raw.pair,
      xfgAmount,
      filledAmount,
      remaining,
      rateNum,
      rateXfgPerCtr,
      isSell,
      isSoftOrder: !!(raw.isSoftOrder || raw.is_soft_order),
      postedHeight,
      ttlBlocks,
      timestamp: Number(raw.timestamp || 0),
      blocksLeft,
      fairPct: fairPctFor(pairKey, rateXfgPerCtr)
    };
  }

  function fairPctFor(pairKey, rateXfgPerCtr) {
    if (!pairKey || !rateXfgPerCtr) return 0;
    const o = oraclePrices[pairKey];
    // oracle price may be XFG-per-CTR in atomic or whole; try both
    let fair = null;
    if (o && o.price != null) {
      const p = Number(o.price);
      fair = p > 1e4 ? p / App.COIN : p;
    } else if (o && o.rate != null) {
      fair = Number(o.rate);
    }
    if (!fair || fair <= 0) return 0;
    return ((rateXfgPerCtr - fair) / fair) * 100;
  }

  function markerSize(remainingAtomic) {
    const whole = remainingAtomic / App.COIN;
    // log scale: 1 XFG → ~12px, 100 → ~22, 10000 → ~32
    const px = 12 + 6 * Math.log10(Math.max(whole, 0.1) + 1);
    return Math.max(12, Math.min(34, px));
  }

  function markerOpacity(offer) {
    if (offer.blocksLeft == null) return 0.9;
    // ~8 blocks ≈ 1h on Fuego-ish cadence used elsewhere
    if (offer.blocksLeft <= 0) return 0.3;
    if (offer.blocksLeft < 8) return 0.45;
    if (offer.blocksLeft < 64) return 0.65;
    return 0.95;
  }

  // ── Init ──

  function init() {
    initOracleChart();
    initChainSelect();
    initBridgeForm();
    initSwapDirection();
    initOrdergraphShell();
    loadOffersAndSwaps();
    loadChainRates();
    loadSPVStatus();
    loadWalletBalance();

    App.on('swap_update', (data) => {
      if (data) applySwapdPayload(data);
    });
    App.on('spv_status', updateSPVFromWS);
    App.on('block', () => {
      loadOffersAndSwaps();
      loadChainRates();
    });

    setInterval(loadOffersAndSwaps, 8000);
    setInterval(loadChainRates, 30000);
  }

  function initOrdergraphShell() {
    const chains = orderedChains();
    const cols = document.getElementById('ordergraph-cols');
    const axis = document.getElementById('ordergraph-axis');
    cols.style.gridTemplateColumns = `repeat(${chains.length}, minmax(36px, 1fr))`;
    axis.style.gridTemplateColumns = `repeat(${chains.length}, minmax(36px, 1fr))`;

    cols.innerHTML = chains.map(k =>
      `<div class="ordergraph-col" data-chain="${k}"></div>`
    ).join('');

    axis.innerHTML = chains.map(k => {
      const c = CHAIN_INFO[k];
      return `<div class="ordergraph-axis-cell empty" data-chain="${k}">
        <div class="ordergraph-depth"><i style="width:0%;background:${c.color}"></i></div>
        <div class="chain-name">${c.name}</div>
        <div class="chain-ticker">${c.ticker}</div>
        <div class="chain-count">0</div>
      </div>`;
    }).join('');
  }

  // ── Oracle Chart ──

  function initOracleChart() {
    const container = document.getElementById('oracle-chart');
    if (!container || typeof LightweightCharts === 'undefined') return;
    oracleChart = LightweightCharts.createChart(container, {
      layout: { background: { color: '#0a0a0f' }, textColor: '#8888a0' },
      grid: { vertLines: { visible: false }, horzLines: { color: 'rgba(42,42,58,0.3)' } },
      rightPriceScale: { borderColor: '#2a2a3a' },
      timeScale: { borderColor: '#2a2a3a', timeVisible: true },
      width: container.clientWidth,
      height: 200
    });
    oracleLineSeries = oracleChart.addLineSeries({
      color: '#ff6b35', lineWidth: 2, priceLineVisible: false
    });
    new ResizeObserver(() => {
      oracleChart.applyOptions({ width: container.clientWidth });
    }).observe(container);
    loadOracleHistory();
  }

  async function loadOracleHistory() {
    try {
      const candles = await App.rpc('get_ohlvc', { timeframe: '1h', count: 168 });
      if (candles && candles.candles && oracleLineSeries) {
        const data = candles.candles.map(c => ({
          time: c.t,
          value: c.c / App.COIN
        }));
        oracleLineSeries.setData(data);
      }
    } catch (e) {
      console.warn('Oracle history load failed:', e);
    }
  }

  // ── Chain Selection ──

  function initChainSelect() {
    const select = document.getElementById('to-chain-select');
    select.addEventListener('change', () => {
      const chain = select.value;
      const info = CHAIN_INFO[chain];
      const iconImg = document.getElementById('to-chain-icon');
      iconImg.src = info.icon;
      iconImg.alt = info.ticker;
      document.getElementById('to-chain-name').textContent = info.name;
      document.getElementById('to-chain-ticker').textContent = info.ticker;
      updateEstimate();
    });
    select.dispatchEvent(new Event('change'));
  }

  // ── Bridge Form ──

  function initBridgeForm() {
    document.getElementById('bridge-amount').addEventListener('input', updateEstimate);
    document.getElementById('bridge-init-btn').addEventListener('click', showInitiateModal);
    document.getElementById('init-modal-close').addEventListener('click', () => {
      document.getElementById('init-modal').classList.remove('active');
    });

    document.getElementById('init-exec-btn').addEventListener('click', async () => {
      const amount = document.getElementById('bridge-amount').value;
      const chain = document.getElementById('to-chain-select').value;
      const peerKey = document.getElementById('peer-pubkey').value;
      if (!amount || parseFloat(amount) <= 0) { App.showToast('Enter a valid amount'); return; }
      try {
        const atomicAmount = Math.round(parseFloat(amount) * App.COIN);
        const result = await App.walletRpc('initiate_swap', {
          xfgAmount: atomicAmount,
          peerPubKey: peerKey || '',
          pair: chain,
          role: 'bob'
        });
        App.showToast(`Swap initiated! ID: ${result.swapId ? result.swapId.substring(0, 16) : 'pending'}`);
        document.getElementById('init-modal').classList.remove('active');
        loadOffersAndSwaps();
      } catch (e) {
        App.showToast(`Error: ${e.message}`);
      }
    });

    document.getElementById('init-copy-btn').addEventListener('click', () => {
      App.copyToClipboard(document.getElementById('init-cli-cmd').textContent);
    });
  }

  function updateEstimate() {
    const amount = parseFloat(document.getElementById('bridge-amount').value) || 0;
    const chain = document.getElementById('to-chain-select').value;
    const fee = amount * 0.02;
    const output = amount - fee;
    document.getElementById('bridge-estimated').textContent = output > 0 ? output.toFixed(4) : '0.00';
    document.getElementById('bridge-rate').textContent = amount > 0
      ? `1 XFG ≈ ${(output / amount).toFixed(4)} ${CHAIN_INFO[chain].ticker} (after fees)`
      : 'Select an amount to see the rate';
  }

  function initSwapDirection() {
    document.getElementById('bridge-swap-dir').addEventListener('click', () => {
      swapDirection = swapDirection === 0 ? 1 : 0;
      document.getElementById('bridge-swap-dir').textContent = swapDirection === 0 ? '↓' : '↑';
    });
  }

  async function loadWalletBalance() {
    try {
      const bal = await App.walletRpc('getbalance');
      if (bal && bal.availableBalance != null) {
        const s = App.fmtXfg(bal.availableBalance);
        document.getElementById('xfg-balance').textContent = s;
        document.getElementById('bridge-available').textContent = `Available: ${s} XFG`;
      }
    } catch {
      // wallet offline
    }
  }

  // ── Offers + swaps from swapd ──

  async function loadOffersAndSwaps() {
    try {
      const data = await App.daemonGet('/api/swapd/');
      applySwapdPayload(data);
    } catch {
      // swapd offline — keep last state
    }
  }

  function applySwapdPayload(data) {
    if (!data || typeof data !== 'object') return;
    if (data.height != null) chainHeight = Number(data.height) || chainHeight;

    const rawOffers = data.offers || [];
    offers = rawOffers.map(normalizeOffer).filter(o => o.pairKey && o.remaining > 0);

    // Recompute fair % after height/oracle
    offers.forEach(o => { o.fairPct = fairPctFor(o.pairKey, o.rateXfgPerCtr); });

    renderOrdergraph();
    renderOrderbook();

    const swaps = data.active_swaps || data.swaps || [];
    renderSwaps(swaps);
  }

  // ── Ordergraph render ──

  function renderOrdergraph() {
    const chains = orderedChains();
    const byChain = {};
    chains.forEach(k => { byChain[k] = []; });
    offers.forEach(o => {
      if (byChain[o.pairKey]) byChain[o.pairKey].push(o);
    });

    // Y: fairPct mapped so 0% = mid. Clamp ±15% for scale (or expand if outliers)
    let maxAbs = 5;
    offers.forEach(o => {
      maxAbs = Math.max(maxAbs, Math.abs(o.fairPct || 0));
    });
    maxAbs = Math.min(50, Math.max(5, maxAbs * 1.15));

    let totalDepth = 0;
    const depths = {};
    chains.forEach(k => {
      depths[k] = byChain[k].reduce((s, o) => s + o.remaining, 0);
      totalDepth += depths[k];
    });

    document.getElementById('offer-count').textContent = `${offers.length} open`;

    chains.forEach(k => {
      const col = document.querySelector(`.ordergraph-col[data-chain="${k}"]`);
      const axis = document.querySelector(`.ordergraph-axis-cell[data-chain="${k}"]`);
      if (!col || !axis) return;
      const list = byChain[k];
      const info = CHAIN_INFO[k];

      const usedY = {};
      col.innerHTML = '';
      list.forEach(o => {
        const pct = Math.max(-maxAbs, Math.min(maxAbs, o.fairPct || 0));
        // bottom% : 50% = fair; higher fairPct → higher on plot
        const y = 50 + (pct / maxAbs) * 45;
        const yKey = Math.round(y);
        usedY[yKey] = (usedY[yKey] || 0) + 1;
        const jitter = (usedY[yKey] - 1) * 3;
        const size = markerSize(o.remaining);
        const opacity = markerOpacity(o);
        const logo = o.isSell ? FUEGO_ICON : info.icon;
        const el = document.createElement('div');
        el.className = 'ordergraph-marker' + (o.isSell ? ' sell-xfg' : '') +
          (o.offerId === selectedOfferId ? ' selected' : '');
        el.dataset.offerId = o.offerId;
        el.style.bottom = y + '%';
        el.style.marginLeft = jitter + 'px';
        el.style.width = size + 'px';
        el.style.height = size + 'px';
        el.style.background = info.color;
        el.style.opacity = String(opacity);
        el.setAttribute('role', 'button');
        el.tabIndex = 0;
        const img = document.createElement('img');
        img.src = logo;
        img.alt = o.isSell ? 'XFG' : info.ticker;
        el.appendChild(img);
        el.addEventListener('mouseenter', (ev) => showTooltip(ev, o));
        el.addEventListener('mousemove', moveTooltip);
        el.addEventListener('mouseleave', hideTooltip);
        el.addEventListener('click', () => selectOffer(o.offerId, true));
        el.addEventListener('keydown', (ev) => {
          if (ev.key === 'Enter' || ev.key === ' ') {
            ev.preventDefault();
            selectOffer(o.offerId, true);
          }
        });
        col.appendChild(el);
      });

      const depthPct = totalDepth > 0 ? (depths[k] / totalDepth) * 100 : 0;
      axis.classList.toggle('empty', list.length === 0);
      axis.querySelector('.ordergraph-depth > i').style.width = depthPct + '%';
      axis.querySelector('.chain-count').textContent = list.length ? `${list.length} open` : '0';
    });
  }

  function escapeAttr(s) {
    return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
  }

  function showTooltip(e, o) {
    const tip = document.getElementById('ordergraph-tooltip');
    const info = CHAIN_INFO[o.pairKey] || {};
    const side = o.isSell ? 'Sell XFG → ' + (info.ticker || o.pairKey) : 'Buy XFG ← ' + (info.ticker || o.pairKey);
    const fair = (o.fairPct >= 0 ? '+' : '') + o.fairPct.toFixed(2) + '% vs fair';
    const exp = o.blocksLeft != null ? `~${o.blocksLeft} blocks left` : 'expiry n/a';
    tip.innerHTML = `<strong>${side}</strong><br>` +
      `${App.fmtXfg(o.remaining)} XFG remaining<br>` +
      `rate ${o.rateXfgPerCtr.toFixed(4)} XFG/${info.ticker || '?'} · ${fair}<br>` +
      `${exp}${o.isSoftOrder ? ' · soft' : ''}`;
    tip.hidden = false;
    moveTooltip(e);
  }

  function moveTooltip(e) {
    const tip = document.getElementById('ordergraph-tooltip');
    if (tip.hidden) return;
    const x = e.clientX + 12;
    const y = e.clientY + 12;
    tip.style.left = x + 'px';
    tip.style.top = y + 'px';
  }

  function hideTooltip() {
    document.getElementById('ordergraph-tooltip').hidden = true;
  }

  // ── Orderbook list ──

  function renderOrderbook() {
    const list = document.getElementById('orderbook-list');
    const empty = document.getElementById('orderbook-empty');
    const count = document.getElementById('book-count');
    count.textContent = String(offers.length);

    if (!offers.length) {
      list.innerHTML = '';
      empty.style.display = '';
      return;
    }
    empty.style.display = 'none';

    // Sort: selected first, then by |fair| asc, then remaining desc
    const sorted = offers.slice().sort((a, b) => {
      if (a.offerId === selectedOfferId) return -1;
      if (b.offerId === selectedOfferId) return 1;
      return Math.abs(a.fairPct) - Math.abs(b.fairPct) || b.remaining - a.remaining;
    });

    list.innerHTML = sorted.map(o => {
      const info = CHAIN_INFO[o.pairKey] || { icon: '', color: '#888', ticker: '?', name: o.pairKey };
      const logo = o.isSell ? FUEGO_ICON : info.icon;
      const side = o.isSell ? `Sell XFG → ${info.ticker}` : `Buy XFG ← ${info.ticker}`;
      const sel = o.offerId === selectedOfferId ? ' selected' : '';
      const sellCls = o.isSell ? ' sell-xfg' : '';
      const idShort = o.offerId ? o.offerId.substring(0, 16) + (o.offerId.length > 16 ? '…' : '') : '—';
      const fairStr = (o.fairPct >= 0 ? '+' : '') + o.fairPct.toFixed(2) + '%';
      return `
        <div class="ob-row${sel}" id="ob-row-${cssId(o.offerId)}" data-offer-id="${escapeAttr(o.offerId)}">
          <div class="ob-main">
            <div class="ob-top">
              <div class="ob-marker${sellCls}" style="background:${info.color}">
                <img src="${logo}" alt="">
              </div>
              <div>
                <div class="ob-title">${side} · ${info.name}</div>
                <div class="ob-sub">${idShort}${o.isSoftOrder ? ' · soft order' : ''}</div>
              </div>
              <span class="badge ${o.isSell ? 'badge-yellow' : 'badge-blue'}">${o.isSell ? 'SELL XFG' : 'BUY XFG'}</span>
            </div>
            <div class="ob-grid">
              <div><span class="k">Remaining</span><span class="v">${App.fmtXfg(o.remaining)} XFG</span></div>
              <div><span class="k">Original</span><span class="v">${App.fmtXfg(o.xfgAmount)} XFG</span></div>
              <div><span class="k">Filled</span><span class="v">${App.fmtXfg(o.filledAmount)} XFG</span></div>
              <div><span class="k">Rate</span><span class="v">${o.rateXfgPerCtr.toFixed(6)} / ${info.ticker}</span></div>
              <div><span class="k">vs Fair</span><span class="v">${fairStr}</span></div>
              <div><span class="k">Expiry</span><span class="v">${o.blocksLeft != null ? o.blocksLeft + ' blks' : '—'}</span></div>
              <div><span class="k">Posted</span><span class="v">${o.postedHeight ? '#' + o.postedHeight : '—'}</span></div>
            </div>
          </div>
          <div class="ob-actions">
            <button type="button" class="btn btn-primary" data-act="accept" data-id="${escapeAttr(o.offerId)}">Accept offer</button>
            <button type="button" class="btn btn-secondary" data-act="fill" data-id="${escapeAttr(o.offerId)}">Fill form</button>
            <button type="button" class="btn btn-secondary" data-act="copy" data-id="${escapeAttr(o.offerId)}">Copy ID</button>
          </div>
        </div>`;
    }).join('');

    list.querySelectorAll('.ob-row').forEach(row => {
      row.addEventListener('click', (e) => {
        if (e.target.closest('button')) return;
        selectOffer(row.dataset.offerId, false);
      });
    });
    list.querySelectorAll('button[data-act]').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const id = btn.dataset.id;
        const act = btn.dataset.act;
        if (act === 'accept') acceptOffer(id);
        else if (act === 'fill') fillFormFromOffer(id);
        else if (act === 'copy') {
          App.copyToClipboard(id);
        }
      });
    });
  }

  function cssId(id) {
    return String(id).replace(/[^a-zA-Z0-9_-]/g, '_');
  }

  function selectOffer(offerId, scrollToRow) {
    selectedOfferId = offerId;
    const o = offers.find(x => x.offerId === offerId);
    renderOrdergraph();
    renderOrderbook();
    updateSelectedSummary(o);

    if (scrollToRow) {
      const row = document.getElementById('ob-row-' + cssId(offerId));
      const section = document.getElementById('orderbook-section');
      if (section) section.scrollIntoView({ behavior: 'smooth', block: 'start' });
      if (row) {
        setTimeout(() => row.scrollIntoView({ behavior: 'smooth', block: 'nearest' }), 120);
      }
    }
  }

  function updateSelectedSummary(o) {
    const el = document.getElementById('selected-offer-summary');
    if (!o) {
      el.hidden = true;
      el.textContent = '';
      return;
    }
    const info = CHAIN_INFO[o.pairKey] || {};
    el.hidden = false;
    el.textContent = `Selected: ${o.isSell ? 'Sell' : 'Buy'} · ${App.fmtXfg(o.remaining)} XFG · ${info.name || o.pairKey} · ${o.offerId.substring(0, 20)}…`;
  }

  function fillFormFromOffer(offerId) {
    const o = offers.find(x => x.offerId === offerId);
    if (!o) return;
    selectOffer(offerId, false);
    if (o.pairKey) {
      const sel = document.getElementById('to-chain-select');
      if ([...sel.options].some(opt => opt.value === o.pairKey)) {
        sel.value = o.pairKey;
        sel.dispatchEvent(new Event('change'));
      }
    }
    document.getElementById('bridge-amount').value = (o.remaining / App.COIN).toFixed(4);
    updateEstimate();
    App.showToast('Form filled from offer');
  }

  async function acceptOffer(offerId) {
    const o = offers.find(x => x.offerId === offerId);
    if (!o) {
      App.showToast('Offer not found');
      return;
    }
    selectOffer(offerId, true);
    fillFormFromOffer(offerId);

    // Soft-order take path not yet a dedicated wallet RPC — initiate with size of remaining.
    try {
      const peerKey = document.getElementById('peer-pubkey').value;
      const result = await App.walletRpc('initiate_swap', {
        xfgAmount: o.remaining,
        peerPubKey: peerKey || '',
        pair: o.pairKey,
        role: o.isSell ? 'alice' : 'bob', // taker opposite of maker side
        offerId: o.offerId
      });
      App.showToast(`Accept sent · ${result.swapId ? result.swapId.substring(0, 16) : 'ok'}`);
      loadOffersAndSwaps();
    } catch (e) {
      App.showToast(`Accept: ${e.message} — use Fill form + Post, or CLI`);
      const cmd = `xfg-swapd / fire_wallet — take offer ${o.offerId} amount ${o.remaining}`;
      console.info(cmd);
    }
  }

  // ── Initiate Modal ──

  function showInitiateModal() {
    const amount = document.getElementById('bridge-amount').value;
    const chain = document.getElementById('to-chain-select').value;
    const peerKey = document.getElementById('peer-pubkey').value;
    if (!amount || parseFloat(amount) <= 0) { App.showToast('Enter an amount'); return; }

    const info = CHAIN_INFO[chain];
    const fee = parseFloat(amount) * 0.02;
    const output = parseFloat(amount) - fee;

    document.getElementById('init-modal-details').innerHTML = `
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;">
        <div class="metric"><span class="metric-label">From</span><span class="metric-value accent">${amount} XFG</span></div>
        <div class="metric"><span class="metric-label">To</span><span class="metric-value" style="color:${info.color}">${output.toFixed(4)} ${info.ticker}</span></div>
        <div class="metric"><span class="metric-label">Chain</span><span class="metric-value">${info.name}</span></div>
        <div class="metric"><span class="metric-label">Fee</span><span class="metric-value red">${fee.toFixed(4)} XFG</span></div>
      </div>`;

    document.getElementById('init-cli-cmd').textContent =
      `fire_wallet initiate_swap ${Math.round(parseFloat(amount) * App.COIN)} ${peerKey || '<peer_pubkey>'} ${chain} bob`;
    document.getElementById('init-cli-cmd').style.display = '';

    document.getElementById('init-modal').classList.add('active');
  }

  // ── Active Swaps ──

  function renderSwaps(swaps) {
    const list = document.getElementById('active-swaps-list');
    const empty = document.getElementById('swaps-empty');
    const count = document.getElementById('active-count');

    if (!swaps || swaps.length === 0) {
      list.innerHTML = '';
      empty.style.display = '';
      count.textContent = '0';
      return;
    }

    empty.style.display = 'none';
    count.textContent = swaps.length;

    list.innerHTML = swaps.map(s => {
      const pairKey = pairKeyFromIndex(s.pair) || s.pair;
      const info = CHAIN_INFO[pairKey] || { icon: '/coin-icons/fuego.png', color: '#888', ticker: String(s.pair), name: String(s.pair) };
      const state = getSwapStateInfo(s.state);
      const steps = getSwapSteps(s.state);
      const sid = s.swapId || s.swap_id || '';

      return `
        <div style="padding:16px;border-bottom:1px solid var(--border);">
          <div style="display:flex;align-items:center;gap:12px;margin-bottom:12px;">
            <img src="${info.icon}" alt="" style="width:32px;height:32px;border-radius:50%;object-fit:contain;background:${info.color}33;">
            <div style="flex:1;">
              <div style="font-weight:600;font-size:14px;">XFG ↔ ${info.ticker}</div>
              <div style="font-size:12px;color:var(--text-muted);font-family:var(--font-mono);">${sid ? sid.substring(0, 16) + '…' : '—'}</div>
            </div>
            <span class="badge ${state.badge}">${state.label}</span>
          </div>
          <div class="progress-steps" style="padding:0;">
            ${steps.map((step, i) => `
              <div class="step ${step.status}">
                <div class="step-dot">${step.status === 'done' ? '✓' : (i + 1)}</div>
                <div class="step-label">${step.label}</div>
              </div>
              ${i < steps.length - 1 ? `<div class="step-line ${step.status === 'done' ? 'done' : (step.status === 'active' ? 'active' : '')}"></div>` : ''}
            `).join('')}
          </div>
          ${s.error ? `<div style="margin-top:8px;font-size:12px;color:var(--red);">${s.error}</div>` : ''}
        </div>`;
    }).join('');
  }

  function getSwapStateInfo(state) {
    const map = {
      INITIATED:                   { label: 'Initiated', badge: 'badge-blue' },
      ADAPTOR_KEYS_EXCHANGED:     { label: 'Keys Exchanged', badge: 'badge-blue' },
      ADAPTOR_ESCROW_FUNDED:      { label: 'Escrow Funded', badge: 'badge-yellow' },
      ADAPTOR_PRESIGS_READY:      { label: 'Pre-sigs Ready', badge: 'badge-yellow' },
      ADAPTOR_WAITING_SPV:        { label: 'Verifying (SPV)', badge: 'badge-blue' },
      ADAPTOR_SECRET_CONFIRMED_SPV:{ label: 'SPV Verified', badge: 'badge-green' },
      ADAPTOR_CTR_LOCKED:         { label: 'Counterparty Locked', badge: 'badge-yellow' },
      ADAPTOR_SECRET_REVEALED:    { label: 'Secret Revealed', badge: 'badge-green' },
      ADAPTOR_XFG_SPENT:          { label: 'Completed', badge: 'badge-green' },
      ADAPTOR_REFUNDED:           { label: 'Refunded', badge: 'badge-red' },
      FAILED:                     { label: 'Failed', badge: 'badge-red' },
      AFK_OFFER_LOCKED:           { label: 'AFK Locked', badge: 'badge-blue' },
      AFK_OFFER_ACCEPTED:         { label: 'AFK Accepted', badge: 'badge-yellow' },
      AFK_CLAIMED:                { label: 'AFK Completed', badge: 'badge-green' },
      AFK_REFUNDED:               { label: 'AFK Refunded', badge: 'badge-red' }
    };
    return map[state] || { label: state || 'Unknown', badge: 'badge-blue' };
  }

  function getSwapSteps(state) {
    const stepDefs = [
      { label: 'Init', states: ['INITIATED'] },
      { label: 'Keys', states: ['ADAPTOR_KEYS_EXCHANGED'] },
      { label: 'Escrow', states: ['ADAPTOR_ESCROW_FUNDED', 'AFK_OFFER_LOCKED'] },
      { label: 'Lock CTR', states: ['ADAPTOR_CTR_LOCKED', 'ADAPTOR_WAITING_SPV', 'AFK_OFFER_ACCEPTED'] },
      { label: 'Claim', states: ['ADAPTOR_SECRET_REVEALED', 'ADAPTOR_SECRET_CONFIRMED_SPV'] },
      { label: 'Done', states: ['ADAPTOR_XFG_SPENT', 'AFK_CLAIMED'] }
    ];
    const terminalOk = ['ADAPTOR_XFG_SPENT', 'AFK_CLAIMED'];
    const terminalFail = ['ADAPTOR_REFUNDED', 'AFK_REFUNDED', 'FAILED'];
    if (terminalFail.includes(state)) {
      return stepDefs.map(s => ({ ...s, status: 'error' }));
    }
    let reached = -1;
    for (let i = 0; i < stepDefs.length; i++) {
      if (stepDefs[i].states.includes(state)) { reached = i; break; }
    }
    if (terminalOk.includes(state)) reached = stepDefs.length - 1;
    return stepDefs.map((s, i) => ({
      ...s,
      status: i < reached ? 'done' : i === reached ? (terminalOk.includes(state) ? 'done' : 'active') : ''
    }));
  }

  // ── Chain Rates ──

  async function loadChainRates() {
    try {
      const data = await App.daemonGet('/getswapprice');
      if (data && data.prices) {
        oraclePrices = data.prices;
        const tbody = document.getElementById('chain-rates');
        tbody.innerHTML = Object.entries(data.prices).map(([chain, info]) => {
          const key = pairKeyFromIndex(chain) || chain;
          const ci = CHAIN_INFO[key] || { icon: '', color: '#888', ticker: chain };
          return `<tr>
            <td><img src="${ci.icon || ''}" alt="" style="width:16px;height:16px;border-radius:50%;vertical-align:middle;margin-right:6px;object-fit:contain;">${ci.ticker}</td>
            <td style="text-align:right">${info.price != null ? App.fmtPrice(info.price) : '—'}</td>
            <td style="text-align:right">${info.spread != null ? App.fmtPct(info.spread) : '—'}</td>
          </tr>`;
        }).join('');
        // refresh fair positions if we have offers
        if (offers.length) {
          offers.forEach(o => { o.fairPct = fairPctFor(o.pairKey, o.rateXfgPerCtr); });
          renderOrdergraph();
          renderOrderbook();
        }
      }
    } catch {
      // oracle offline
    }
  }

  // ── SPV ──

  function loadSPVStatus() {
    App.on('spv_status', updateSPVFromWS);
  }

  function updateSPVFromWS(data) {
    if (!data) return;
    if (data.header_height) document.getElementById('spv-height').textContent = data.header_height.toLocaleString();
    if (data.verified_txs != null) document.getElementById('spv-verified').textContent = data.verified_txs.toLocaleString();
    if (data.peer_count != null) document.getElementById('spv-peers').textContent = data.peer_count;
    if (data.chain) document.getElementById('spv-chain').textContent = data.chain;
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', SwapXFG.init);
