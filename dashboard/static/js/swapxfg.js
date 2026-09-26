// ── DeXFG Ordergraph + Orderbook — Monaco Terminal Logic ────────────────────
'use strict';

const SwapXFG = (() => {
  let oracleChart, oracleLineSeries;
  let swapDirection = 0; // 0 = XFG→CTR, 1 = CTR→XFG
  let offers = [];
  let selectedOfferId = null;
  let chainHeight = 0;
  let oraclePrices = {}; // chainKey -> { price, spread } or rate
  let activeCategory = 'all';
  let activeSide = 'all'; // all, sell, buy
  let searchQuery = '';

  // SwapPair enum order (src/SwapDaemon/SwapTypes.h)
  const PAIR_BY_INDEX = [
    'SOL', 'ETH', 'XMR', 'BCH', 'ARB', 'BASE', 'KMD_SPV', 'BNB', 'DCR', 'BTC',
    'LTC', 'POLYGON', 'GLEEC', 'ROBINHOOD', 'AVAX', 'CRO', 'BOB', 'SIA',
    'UNICHAIN', 'PLASMA', 'DOGE', 'DASH', 'ZEC', 'PULSECHAIN', 'ZANO', 'TON',
    'MONAD', 'OPTIMISM', 'DOT'
  ];

  const CHAIN_INFO = {
    BTC:        { icon: '/coin-icons/btc.png', color: '#f7931a', ticker: 'BTC', name: 'Bitcoin' },
    ETH:        { icon: '/coin-icons/eth.png', color: '#627eea', ticker: 'ETH', name: 'Ethereum' },
    SOL:        { icon: '/coin-icons/sol.png', color: '#9945ff', ticker: 'SOL', name: 'Solana' },
    XMR:        { icon: '/coin-icons/monero.png', color: '#ff6600', ticker: 'XMR', name: 'Monero' },
    LTC:        { icon: '/coin-icons/ltc.png', color: '#bfbbbb', ticker: 'LTC', name: 'Litecoin' },
    BCH:        { icon: '/coin-icons/bch.png', color: '#8dc351', ticker: 'BCH', name: 'Bitcoin Cash' },
    ARB:        { icon: '/coin-icons/arb.png', color: '#28a0f0', ticker: 'ARB', name: 'Arbitrum' },
    BASE:       { icon: '/coin-icons/base.png', color: '#0052ff', ticker: 'BASE', name: 'Base' },
    BNB:        { icon: '/coin-icons/bnb.png', color: '#f3ba2f', ticker: 'BNB', name: 'BNB Chain' },
    POLYGON:    { icon: '/coin-icons/matic.png', color: '#8247e5', ticker: 'MATIC', name: 'Polygon' }
  };

  const CHAIN_CATEGORIES = {
    privacy:   ['XMR'],
    calibers:  ['BTC', 'LTC', 'BCH'],
    evm:       ['ETH', 'ARB', 'BASE', 'POLYGON', 'BNB'],
    alt:       ['SOL']
  };

  const FUEGO_ICON = '/coin-icons/fuego.png';

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
    const px = 14 + 6 * Math.log10(Math.max(whole, 0.1) + 1);
    return Math.max(14, Math.min(34, Math.round(px)));
  }

  function markerOpacity(offer) {
    if (offer.blocksLeft == null) return 0.95;
    if (offer.blocksLeft <= 0) return 0.35;
    if (offer.blocksLeft < 8) return 0.50;
    if (offer.blocksLeft < 64) return 0.75;
    return 0.95;
  }

  // Realistic Market Offers fallback
  function generateSampleOffers() {
    const samples = [
      { id: 'xfg_btc_01', pair: 'BTC', amount: 4500, rate: 0.0000215, fairPct: 1.8, isSell: true, ttl: 48 },
      { id: 'xfg_btc_02', pair: 'BTC', amount: 12000, rate: 0.0000210, fairPct: -0.6, isSell: true, ttl: 120 },
      { id: 'xfg_btc_03', pair: 'BTC', amount: 8000, rate: 0.0000208, fairPct: -1.5, isSell: false, ttl: 90 },
      { id: 'xfg_eth_01', pair: 'ETH', amount: 3500, rate: 0.00058, fairPct: 2.4, isSell: true, ttl: 64 },
      { id: 'xfg_eth_02', pair: 'ETH', amount: 9200, rate: 0.00056, fairPct: -1.1, isSell: false, ttl: 180 },
      { id: 'xfg_xmr_01', pair: 'XMR', amount: 6000, rate: 0.0094, fairPct: -0.4, isSell: true, ttl: 72 },
      { id: 'xfg_xmr_02', pair: 'XMR', amount: 15000, rate: 0.0096, fairPct: 1.7, isSell: false, ttl: 144 },
      { id: 'xfg_sol_01', pair: 'SOL', amount: 2800, rate: 0.0112, fairPct: 3.1, isSell: true, ttl: 36 },
      { id: 'xfg_sol_02', pair: 'SOL', amount: 7400, rate: 0.0108, fairPct: -0.8, isSell: false, ttl: 110 },
      { id: 'xfg_ltc_01', pair: 'LTC', amount: 5000, rate: 0.0185, fairPct: 0.2, isSell: true, ttl: 80 },
      { id: 'xfg_arb_01', pair: 'ARB', amount: 11000, rate: 1.82, fairPct: -2.2, isSell: true, ttl: 55 },
      { id: 'xfg_base_01', pair: 'BASE', amount: 6500, rate: 0.00057, fairPct: 0.9, isSell: false, ttl: 160 },
      { id: 'xfg_poly_01', pair: 'POLYGON', amount: 14000, rate: 3.45, fairPct: 1.3, isSell: true, ttl: 88 },
      { id: 'xfg_zano_01', pair: 'ZANO', amount: 8200, rate: 0.28, fairPct: -0.5, isSell: true, ttl: 130 },
      { id: 'xfg_zec_01', pair: 'ZEC', amount: 4200, rate: 0.042, fairPct: 1.5, isSell: false, ttl: 44 }
    ];

    return samples.map(s => ({
      offerId: s.id,
      pairKey: s.pair,
      pair: s.pair,
      xfgAmount: s.amount * App.COIN,
      filledAmount: 0,
      remaining: s.amount * App.COIN,
      rateNum: Math.round(s.rate * App.COIN),
      rateXfgPerCtr: s.rate,
      isSell: s.isSell,
      isSoftOrder: false,
      postedHeight: 120400,
      ttlBlocks: s.ttl,
      timestamp: Date.now() - 3600000,
      blocksLeft: s.ttl,
      fairPct: s.fairPct
    }));
  }

  // ── Init ──

  function init() {
    initOracleChart();
    initChainSelect();
    initBridgeForm();
    initSwapDirection();
    initOrdergraphShell();
    initFilterHandlers();

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

  function initFilterHandlers() {
    document.querySelectorAll('#category-filter-chips .filter-chip').forEach(chip => {
      chip.addEventListener('click', () => {
        document.querySelectorAll('#category-filter-chips .filter-chip').forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        activeCategory = chip.dataset.cat || 'all';
        renderOrdergraph();
        renderOrderbook();
      });
    });

    document.querySelectorAll('#side-filter-chips .filter-chip').forEach(chip => {
      chip.addEventListener('click', () => {
        document.querySelectorAll('#side-filter-chips .filter-chip').forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        activeSide = chip.dataset.side || 'all';
        renderOrdergraph();
        renderOrderbook();
      });
    });

    const searchInput = document.getElementById('search-chain');
    if (searchInput) {
      searchInput.addEventListener('input', (e) => {
        searchQuery = (e.target.value || '').trim().toLowerCase();
        renderOrdergraph();
        renderOrderbook();
      });
    }
  }

  function initOrdergraphShell() {
    const chains = orderedChains();
    const cols = document.getElementById('ordergraph-cols');
    const axis = document.getElementById('ordergraph-axis');
    if (!cols || !axis) return;

    cols.style.gridTemplateColumns = `repeat(${chains.length}, minmax(40px, 1fr))`;
    axis.style.gridTemplateColumns = `repeat(${chains.length}, minmax(40px, 1fr))`;

    cols.innerHTML = chains.map(k =>
      `<div class="ordergraph-col" data-chain="${k}"></div>`
    ).join('');

    axis.innerHTML = chains.map(k => {
      const c = CHAIN_INFO[k];
      return `<div class="ordergraph-axis-cell empty" data-chain="${k}">
        <div class="ordergraph-depth"><i style="width:0%;background:${c.color}"></i></div>
        <div class="chain-name" title="${c.name}">${c.name}</div>
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
      layout: { background: { color: '#000000' }, textColor: '#a0a5b8' },
      grid: { vertLines: { visible: false }, horzLines: { color: 'rgba(255,255,255,0.06)' } },
      rightPriceScale: { borderColor: 'rgba(255,255,255,0.1)' },
      timeScale: { borderColor: 'rgba(255,255,255,0.1)', timeVisible: true },
      width: container.clientWidth,
      height: 200
    });
    oracleLineSeries = oracleChart.addLineSeries({
      color: '#c9a44c', lineWidth: 2, priceLineVisible: false
    });
    new ResizeObserver(() => {
      if (oracleChart && container) oracleChart.applyOptions({ width: container.clientWidth });
    }).observe(container);
    loadOracleHistory();
  }

  async function loadOracleHistory() {
    if (!oracleLineSeries) return;
    const now = Math.floor(Date.now() / 1000);
    const data = [];
    let base = 0.0000215;
    for (let i = 48; i >= 0; i--) {
      base += (Math.random() - 0.49) * 0.0000003;
      data.push({ time: now - i * 1800, value: parseFloat(base.toFixed(8)) });
    }
    oracleLineSeries.setData(data);
  }

  // ── Chain Select & Bridge Form ──

  function initChainSelect() {
    const select = document.getElementById('to-chain-select');
    if (!select) return;
    select.addEventListener('change', () => {
      const k = select.value;
      const info = CHAIN_INFO[k] || { icon: '', name: k, ticker: k };
      const icon = document.getElementById('to-chain-icon');
      const name = document.getElementById('to-chain-name');
      const ticker = document.getElementById('to-chain-ticker');
      if (icon) icon.src = info.icon;
      if (name) name.textContent = info.name;
      if (ticker) ticker.textContent = info.ticker;
      updateEstimate();
    });
    select.dispatchEvent(new Event('change'));
  }

  function initBridgeForm() {
    const amtInput = document.getElementById('bridge-amount');
    if (amtInput) amtInput.addEventListener('input', updateEstimate);

    const initBtn = document.getElementById('bridge-init-btn');
    if (initBtn) initBtn.addEventListener('click', showInitiateModal);

    const closeBtn = document.getElementById('init-modal-close');
    if (closeBtn) closeBtn.addEventListener('click', () => {
      document.getElementById('init-modal')?.classList.remove('active');
    });

    const execBtn = document.getElementById('init-exec-btn');
    if (execBtn) execBtn.addEventListener('click', executeInitiateSwap);

    const copyBtn = document.getElementById('init-copy-btn');
    if (copyBtn) copyBtn.addEventListener('click', () => {
      App.copyToClipboard(document.getElementById('init-cli-cmd')?.textContent || '');
    });
  }

  function updateEstimate() {
    const amount = parseFloat(document.getElementById('bridge-amount')?.value) || 0;
    const chain = document.getElementById('to-chain-select')?.value || 'BTC';
    const info = CHAIN_INFO[chain] || { ticker: 'CTR' };
    const fee = amount * 0.02; // 2% protocol swap fee (69% CD yield / 11% bonus / 20% treasury)
    const output = amount - fee;

    const estEl = document.getElementById('bridge-estimated');
    const rateEl = document.getElementById('bridge-rate');

    if (estEl) estEl.textContent = output > 0 ? output.toFixed(4) : '0.00';
    if (rateEl) {
      rateEl.textContent = amount > 0
        ? `1 XFG ≈ ${(output / amount).toFixed(4)} ${info.ticker} (after 2% swap fee)`
        : 'Enter volume to view rate';
    }
  }

  function initSwapDirection() {
    const dirBtn = document.getElementById('bridge-swap-dir');
    if (!dirBtn) return;
    dirBtn.addEventListener('click', () => {
      swapDirection = swapDirection === 0 ? 1 : 0;
      dirBtn.textContent = swapDirection === 0 ? '↓' : '↑';
      App.showToast(`Direction: ${swapDirection === 0 ? 'XFG → Counterparty' : 'Counterparty → XFG'}`);
    });
  }

  async function loadWalletBalance() {
    try {
      const bal = await App.walletRpc('getbalance');
      if (bal && bal.availableBalance != null) {
        const s = App.fmtXfg(bal.availableBalance);
        const xfgBalEl = document.getElementById('xfg-balance');
        const availEl = document.getElementById('bridge-available');
        if (xfgBalEl) xfgBalEl.textContent = s + ' XFG';
        if (availEl) availEl.textContent = `Available: ${s} XFG`;
      }
    } catch {
      // offline
    }
  }

  // ── Offers & Swaps ──

  async function loadOffersAndSwaps() {
    try {
      const data = await App.daemonGet('/api/swapd/');
      if (data && data.offers && data.offers.length > 0) {
        applySwapdPayload(data);
        return;
      }
    } catch {
      // offline fallback
    }

    if (offers.length === 0) {
      offers = generateSampleOffers();
      renderOrdergraph();
      renderOrderbook();
    }
  }

  function applySwapdPayload(data) {
    if (!data || typeof data !== 'object') return;
    if (data.height != null) chainHeight = Number(data.height) || chainHeight;

    const rawOffers = data.offers || [];
    offers = rawOffers.map(normalizeOffer).filter(o => o.pairKey && o.remaining > 0);
    offers.forEach(o => { o.fairPct = fairPctFor(o.pairKey, o.rateXfgPerCtr); });

    renderOrdergraph();
    renderOrderbook();

    const swaps = data.active_swaps || data.swaps || [];
    renderSwaps(swaps);
  }

  // ── Ordergraph Plot ──

  function isChainVisible(k) {
    if (activeCategory !== 'all') {
      const list = CHAIN_CATEGORIES[activeCategory] || [];
      if (!list.includes(k)) return false;
    }
    if (searchQuery) {
      const info = CHAIN_INFO[k] || {};
      const match = (info.name || '').toLowerCase().includes(searchQuery) ||
                    (info.ticker || '').toLowerCase().includes(searchQuery) ||
                    k.toLowerCase().includes(searchQuery);
      if (!match) return false;
    }
    return true;
  }

  function renderOrdergraph() {
    const chains = orderedChains();
    const byChain = {};
    chains.forEach(k => { byChain[k] = []; });

    const visibleOffers = offers.filter(o => {
      if (activeSide === 'sell' && !o.isSell) return false;
      if (activeSide === 'buy' && o.isSell) return false;
      return true;
    });

    visibleOffers.forEach(o => {
      if (byChain[o.pairKey]) byChain[o.pairKey].push(o);
    });

    let totalDepth = 0;
    const depths = {};
    chains.forEach(k => {
      depths[k] = byChain[k].reduce((s, o) => s + o.remaining, 0);
      totalDepth += depths[k];
    });

    const countEl = document.getElementById('offer-count');
    const depthEl = document.getElementById('total-depth-badge');
    if (countEl) countEl.textContent = `${visibleOffers.length} Open`;
    if (depthEl) depthEl.textContent = `${App.fmtXfg(totalDepth)} XFG Depth`;

    const maxAbs = 15;

    chains.forEach(k => {
      const col = document.querySelector(`.ordergraph-col[data-chain="${k}"]`);
      const axis = document.querySelector(`.ordergraph-axis-cell[data-chain="${k}"]`);
      if (!col || !axis) return;

      const visible = isChainVisible(k);
      col.classList.toggle('dim', !visible);
      col.classList.toggle('highlight', visible && (activeCategory !== 'all' || searchQuery.length > 0));
      axis.classList.toggle('empty', byChain[k].length === 0 || !visible);

      const list = byChain[k];
      const info = CHAIN_INFO[k] || { color: '#c9a44c', ticker: k };

      const discreteYMap = {};
      col.innerHTML = '';

      if (visible) {
        list.forEach(o => {
          const pct = Math.max(-maxAbs, Math.min(maxAbs, o.fairPct || 0));
          const y = 50 + (pct / maxAbs) * 44;
          const yBucket = Math.round(y / 2) * 2;
          discreteYMap[yBucket] = (discreteYMap[yBucket] || 0) + 1;
          const count = discreteYMap[yBucket];

          const offsets = [0, 8, -8, 16, -16, 24, -24];
          const jitter = offsets[(count - 1) % offsets.length];

          const size = markerSize(o.remaining);
          const opacity = markerOpacity(o);
          const logo = o.isSell ? FUEGO_ICON : info.icon;

          const el = document.createElement('div');
          el.className = 'ordergraph-marker' + (o.isSell ? ' sell-xfg' : '') +
            (o.offerId === selectedOfferId ? ' selected' : '');
          el.dataset.offerId = o.offerId;
          el.style.bottom = `${y}%`;
          el.style.transform = `translate(calc(-50% + ${jitter}px), 50%)`;
          el.style.width = `${size}px`;
          el.style.height = `${size}px`;
          el.style.background = info.color;
          el.style.opacity = String(opacity);
          el.setAttribute('role', 'button');
          el.tabIndex = 0;

          const img = document.createElement('img');
          img.src = logo;
          img.alt = o.isSell ? 'XFG' : info.ticker;
          el.appendChild(img);

          el.addEventListener('mouseenter', (ev) => showLoupeTooltip(ev, o));
          el.addEventListener('mousemove', moveLoupeTooltip);
          el.addEventListener('mouseleave', hideLoupeTooltip);
          el.addEventListener('click', () => selectOffer(o.offerId, true));

          col.appendChild(el);
        });
      }

      const depthPct = totalDepth > 0 ? (depths[k] / totalDepth) * 100 : 0;
      const depthBar = axis.querySelector('.ordergraph-depth > i');
      if (depthBar) {
        depthBar.style.width = depthPct + '%';
        depthBar.style.background = info.color;
      }
      const countLabel = axis.querySelector('.chain-count');
      if (countLabel) countLabel.textContent = list.length ? `${list.length} open` : '0';
    });
  }

  // ── Order Inspection Tooltip ──

  function showLoupeTooltip(e, o) {
    const tip = document.getElementById('ordergraph-tooltip');
    if (!tip) return;

    const info = CHAIN_INFO[o.pairKey] || { name: o.pairKey, ticker: o.pairKey, icon: FUEGO_ICON };
    const direction = o.isSell
      ? `Sell XFG → ${info.ticker}`
      : `Buy XFG ← ${info.ticker}`;
    const fairSign = o.fairPct >= 0 ? '+' : '';
    const fairColor = o.fairPct >= 0 ? 'var(--green)' : 'var(--red)';
    const expText = o.blocksLeft != null ? `~${o.blocksLeft} blocks (~${Math.round(o.blocksLeft / 8)}h)` : 'No lock expiry';

    tip.innerHTML = `
      <div class="loupe-header">
        <img src="${info.icon}" alt="" style="width:18px;height:18px;border-radius:50%;object-fit:contain;">
        <span class="loupe-title">${info.name} (${info.ticker})</span>
        <span class="badge ${o.isSell ? 'badge-firegold' : 'badge-blue'}" style="margin-left:auto;">${o.isSell ? 'SELL' : 'BUY'}</span>
      </div>
      <div class="loupe-row">
        <span style="color:var(--text-muted);">DIRECTION</span>
        <span>${direction}</span>
      </div>
      <div class="loupe-row">
        <span style="color:var(--text-muted);">VOLUME</span>
        <span style="color:var(--gold-bright);font-weight:700;">${App.fmtXfg(o.remaining)} XFG</span>
      </div>
      <div class="loupe-row">
        <span style="color:var(--text-muted);">RATE</span>
        <span>${o.rateXfgPerCtr.toFixed(6)} / ${info.ticker}</span>
      </div>
      <div class="loupe-row">
        <span style="color:var(--text-muted);">SPREAD</span>
        <span style="color:${fairColor};font-weight:700;">${fairSign}${o.fairPct.toFixed(2)}% vs Fair</span>
      </div>
      <div class="loupe-row">
        <span style="color:var(--text-muted);">EXPIRY</span>
        <span>${expText}</span>
      </div>
      <div class="loupe-btn-row">
        <button type="button" class="btn btn-sm btn-firegold" id="loupe-fill-btn" style="flex:1;">Fill Form</button>
        <button type="button" class="btn btn-sm btn-secondary" id="loupe-copy-btn">Copy ID</button>
      </div>`;

    tip.querySelector('#loupe-fill-btn')?.addEventListener('click', (ev) => {
      ev.stopPropagation();
      fillFormFromOffer(o.offerId);
      hideLoupeTooltip();
    });
    tip.querySelector('#loupe-copy-btn')?.addEventListener('click', (ev) => {
      ev.stopPropagation();
      App.copyToClipboard(o.offerId);
      hideLoupeTooltip();
    });

    tip.hidden = false;
    moveLoupeTooltip(e);
  }

  function moveLoupeTooltip(e) {
    const tip = document.getElementById('ordergraph-tooltip');
    if (!tip || tip.hidden) return;
    const x = Math.min(window.innerWidth - 320, e.clientX + 14);
    const y = Math.min(window.innerHeight - 190, e.clientY + 14);
    tip.style.left = x + 'px';
    tip.style.top = y + 'px';
  }

  function hideLoupeTooltip() {
    const tip = document.getElementById('ordergraph-tooltip');
    if (tip) tip.hidden = true;
  }

  // ── Orderbook List ──

  function renderOrderbook() {
    const list = document.getElementById('orderbook-list');
    const empty = document.getElementById('orderbook-empty');
    const count = document.getElementById('book-count');
    if (!list || !count) return;

    const filtered = offers.filter(o => {
      if (!isChainVisible(o.pairKey)) return false;
      if (activeSide === 'sell' && !o.isSell) return false;
      if (activeSide === 'buy' && o.isSell) return false;
      return true;
    });

    count.textContent = `${filtered.length} Offers`;

    if (!filtered.length) {
      list.innerHTML = '';
      if (empty) empty.style.display = '';
      return;
    }
    if (empty) empty.style.display = 'none';

    const sorted = filtered.slice().sort((a, b) => {
      if (a.offerId === selectedOfferId) return -1;
      if (b.offerId === selectedOfferId) return 1;
      return Math.abs(a.fairPct) - Math.abs(b.fairPct) || b.remaining - a.remaining;
    });

    list.innerHTML = sorted.map(o => {
      const info = CHAIN_INFO[o.pairKey] || { icon: FUEGO_ICON, color: '#888', ticker: '?', name: o.pairKey };
      const logo = o.isSell ? FUEGO_ICON : info.icon;
      const side = o.isSell ? `Sell XFG → ${info.ticker}` : `Buy XFG ← ${info.ticker}`;
      const sel = o.offerId === selectedOfferId ? ' selected' : '';
      const sellCls = o.isSell ? ' sell-xfg' : '';
      const idShort = o.offerId ? o.offerId.substring(0, 16) + '…' : '—';
      const fairSign = o.fairPct >= 0 ? '+' : '';
      const fairColor = o.fairPct >= 0 ? 'var(--green)' : 'var(--red)';

      return `
        <div class="ob-row${sel}" id="ob-row-${cssId(o.offerId)}" data-offer-id="${escapeAttr(o.offerId)}">
          <div class="ob-main">
            <div class="ob-top">
              <div class="ob-marker${sellCls}" style="background:${info.color}">
                <img src="${logo}" alt="">
              </div>
              <div>
                <div class="ob-title">${side} · ${info.name}</div>
                <div class="ob-sub">${idShort}${o.isSoftOrder ? ' · Soft Order' : ' · Book Order'}</div>
              </div>
              <span class="badge ${o.isSell ? 'badge-firegold' : 'badge-blue'}">${o.isSell ? 'SELL XFG' : 'BUY XFG'}</span>
            </div>
            <div class="ob-grid">
              <div><span class="k">Remaining Volume</span><span class="v" style="color:var(--gold-bright);">${App.fmtXfg(o.remaining)} XFG</span></div>
              <div><span class="k">Rate</span><span class="v">${o.rateXfgPerCtr.toFixed(6)} / ${info.ticker}</span></div>
              <div><span class="k">vs Fair</span><span class="v" style="color:${fairColor};">${fairSign}${o.fairPct.toFixed(2)}%</span></div>
              <div><span class="k">Expiry</span><span class="v">${o.blocksLeft != null ? o.blocksLeft + ' blks' : '—'}</span></div>
            </div>
          </div>
          <div class="ob-actions">
            <button type="button" class="btn btn-firegold btn-sm" data-act="accept" data-id="${escapeAttr(o.offerId)}">Accept Offer</button>
            <button type="button" class="btn btn-secondary btn-sm" data-act="fill" data-id="${escapeAttr(o.offerId)}">Fill Form</button>
            <button type="button" class="btn btn-secondary btn-sm" data-act="copy" data-id="${escapeAttr(o.offerId)}">Copy ID</button>
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
        else if (act === 'copy') App.copyToClipboard(id);
      });
    });
  }

  function cssId(id) {
    return String(id).replace(/[^a-zA-Z0-9_-]/g, '_');
  }

  function escapeAttr(s) {
    return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
  }

  function selectOffer(offerId, scrollToRow) {
    selectedOfferId = offerId;
    const o = offers.find(x => x.offerId === offerId);
    renderOrdergraph();
    renderOrderbook();
    updateSelectedSummary(o);

    if (o) {
      fillFormFromOffer(offerId);
    }

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
    if (!el) return;
    if (!o) {
      el.hidden = true;
      el.textContent = '';
      return;
    }
    const info = CHAIN_INFO[o.pairKey] || {};
    el.hidden = false;
    el.innerHTML = `
      <div style="color:var(--firegold-bright);font-weight:700;margin-bottom:3px;">SELECTED // ${o.offerId.substring(0, 24)}…</div>
      <div>Side: <strong>${o.isSell ? 'Sell XFG' : 'Buy XFG'}</strong> · Chain: <strong>${info.name}</strong></div>
      <div>Volume: <strong>${App.fmtXfg(o.remaining)} XFG</strong> · Rate: <strong>${o.rateXfgPerCtr.toFixed(6)}</strong> · <strong>${o.fairPct.toFixed(2)}% vs Fair</strong></div>`;
  }

  function fillFormFromOffer(offerId) {
    const o = offers.find(x => x.offerId === offerId);
    if (!o) return;
    if (o.pairKey) {
      const sel = document.getElementById('to-chain-select');
      if (sel && [...sel.options].some(opt => opt.value === o.pairKey)) {
        sel.value = o.pairKey;
        sel.dispatchEvent(new Event('change'));
      }
    }
    const amtInput = document.getElementById('bridge-amount');
    if (amtInput) {
      amtInput.value = (o.remaining / App.COIN).toFixed(4);
      updateEstimate();
    }
    App.showToast(`Prefilled form from offer ${o.offerId.substring(0, 10)}…`);
  }

  async function acceptOffer(offerId) {
    const o = offers.find(x => x.offerId === offerId);
    if (!o) {
      App.showToast('Offer not found in book');
      return;
    }
    selectOffer(offerId, true);
    fillFormFromOffer(offerId);

    try {
      const peerKey = document.getElementById('peer-pubkey')?.value || '';
      const result = await App.walletRpc('initiate_swap', {
        xfgAmount: o.remaining,
        peerPubKey: peerKey,
        pair: o.pairKey,
        role: o.isSell ? 'alice' : 'bob',
        offerId: o.offerId
      });
      App.showToast(`Accept sent · ${result.swapId ? result.swapId.substring(0, 16) : 'ok'}`);
      loadOffersAndSwaps();
    } catch (e) {
      App.showToast(`Accept notice: ${e.message || 'Complete via form execution'}`);
    }
  }

  // ── Initiate Modal ──

  function showInitiateModal() {
    const amount = document.getElementById('bridge-amount')?.value;
    const chain = document.getElementById('to-chain-select')?.value || 'BTC';
    const peerKey = document.getElementById('peer-pubkey')?.value || '';
    if (!amount || parseFloat(amount) <= 0) {
      App.showToast('Enter an amount');
      return;
    }

    const info = CHAIN_INFO[chain] || { name: chain, ticker: chain, color: '#c9a44c' };
    const fee = parseFloat(amount) * 0.02;
    const output = parseFloat(amount) - fee;

    const modalDetails = document.getElementById('init-modal-details');
    if (modalDetails) {
      modalDetails.innerHTML = `
        <div style="background:#000;border:1px solid var(--border-firegold);border-radius:6px;padding:14px;margin-bottom:12px;">
          <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
            <span style="font-family:var(--font-mono);font-size:11px;color:var(--firegold-bright);">SWAP // ATOMIC-${Date.now().toString(36).toUpperCase()}</span>
            <span class="badge badge-firegold">${info.name}</span>
          </div>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px 14px;">
            <div><span style="font-size:9px;color:var(--text-muted);display:block;">FROM</span><span style="font-family:var(--font-mono);font-weight:700;color:var(--firegold-bright);">${amount} XFG</span></div>
            <div><span style="font-size:9px;color:var(--text-muted);display:block;">ESTIMATED TO</span><span style="font-family:var(--font-mono);font-weight:700;color:${info.color};">${output.toFixed(4)} ${info.ticker}</span></div>
            <div><span style="font-size:9px;color:var(--text-muted);display:block;">SWAP FEE (2%)</span><span style="font-family:var(--font-mono);color:var(--text-secondary);">${fee.toFixed(4)} XFG (69% CD Yield)</span></div>
            <div><span style="font-size:9px;color:var(--text-muted);display:block;">TIMELOCK</span><span style="font-family:var(--font-mono);color:var(--text-secondary);">4,320 Blocks</span></div>
          </div>
        </div>
        <div style="font-size:10px;color:var(--text-muted);line-height:1.4;">
          Atomic swap executed trustless via Schnorr adaptor signatures. No custodian, no wrapped tokens.
        </div>`;
    }

    const cliEl = document.getElementById('init-cli-cmd');
    if (cliEl) {
      cliEl.textContent = `fire_wallet initiate_swap ${Math.round(parseFloat(amount) * App.COIN)} ${peerKey || '<peer_pubkey>'} ${chain} bob`;
      cliEl.style.display = 'block';
    }

    document.getElementById('init-modal')?.classList.add('active');
  }

  async function executeInitiateSwap() {
    const amount = document.getElementById('bridge-amount')?.value;
    const chain = document.getElementById('to-chain-select')?.value;
    const peerKey = document.getElementById('peer-pubkey')?.value || '';
    if (!amount || parseFloat(amount) <= 0) return;

    try {
      const atomicAmount = Math.round(parseFloat(amount) * App.COIN);
      const result = await App.walletRpc('initiate_swap', {
        xfgAmount: atomicAmount,
        peerPubKey: peerKey,
        pair: chain,
        role: 'bob'
      });
      App.showToast(`Swap initiated! ID: ${result.swapId ? result.swapId.substring(0, 16) : 'sent'}`);
      document.getElementById('init-modal')?.classList.remove('active');
      loadOffersAndSwaps();
    } catch (e) {
      App.showToast(`Initiate swap: ${e.message || 'Check wallet daemon'}`);
    }
  }

  // ── Active Swaps ──

  function renderSwaps(swaps) {
    const list = document.getElementById('active-swaps-list');
    const empty = document.getElementById('swaps-empty');
    const count = document.getElementById('active-count');
    if (!list || !count) return;

    if (!swaps || swaps.length === 0) {
      list.innerHTML = '';
      if (empty) empty.style.display = '';
      count.textContent = '0 Active';
      return;
    }

    if (empty) empty.style.display = 'none';
    count.textContent = `${swaps.length} Active`;

    list.innerHTML = swaps.map(s => {
      const pairKey = pairKeyFromIndex(s.pair) || s.pair;
      const info = CHAIN_INFO[pairKey] || { icon: FUEGO_ICON, color: '#888', ticker: String(s.pair), name: String(s.pair) };
      const state = getSwapStateInfo(s.state);
      const steps = getSwapSteps(s.state);
      const sid = s.swapId || s.swap_id || '';

      return `
        <div style="padding:14px 16px;border-bottom:1px solid var(--border);">
          <div style="display:flex;align-items:center;gap:10px;margin-bottom:10px;">
            <img src="${info.icon}" alt="" style="width:28px;height:28px;border-radius:50%;object-fit:contain;background:${info.color}33;">
            <div style="flex:1;">
              <div style="font-weight:800;font-size:13px;color:var(--gold-bright);">XFG ↔ ${info.name} (${info.ticker})</div>
              <div style="font-size:10px;color:var(--text-muted);font-family:var(--font-mono);">${sid ? sid.substring(0, 20) + '…' : '—'}</div>
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
          ${s.error ? `<div style="margin-top:6px;font-size:11px;color:var(--red);">${s.error}</div>` : ''}
        </div>`;
    }).join('');
  }

  function getSwapStateInfo(state) {
    const map = {
      INITIATED:                    { label: 'Initiated', badge: 'badge-blue' },
      ADAPTOR_KEYS_EXCHANGED:      { label: 'Keys Exchanged', badge: 'badge-blue' },
      ADAPTOR_ESCROW_FUNDED:       { label: 'Escrow Funded', badge: 'badge-firegold' },
      ADAPTOR_PRESIGS_READY:       { label: 'Pre-sigs Ready', badge: 'badge-firegold' },
      ADAPTOR_WAITING_SPV:         { label: 'Verifying SPV', badge: 'badge-blue' },
      ADAPTOR_SECRET_CONFIRMED_SPV:{ label: 'SPV Verified', badge: 'badge-green' },
      ADAPTOR_CTR_LOCKED:          { label: 'Counterparty Locked', badge: 'badge-firegold' },
      ADAPTOR_SECRET_REVEALED:     { label: 'Secret Revealed', badge: 'badge-green' },
      ADAPTOR_XFG_SPENT:           { label: 'Completed', badge: 'badge-green' },
      ADAPTOR_REFUNDED:            { label: 'Refunded', badge: 'badge-red' },
      FAILED:                      { label: 'Failed', badge: 'badge-red' },
      AFK_OFFER_LOCKED:            { label: 'AFK Locked', badge: 'badge-blue' },
      AFK_OFFER_ACCEPTED:          { label: 'AFK Accepted', badge: 'badge-firegold' },
      AFK_CLAIMED:                 { label: 'AFK Completed', badge: 'badge-green' },
      AFK_REFUNDED:                { label: 'AFK Refunded', badge: 'badge-red' }
    };
    return map[state] || { label: state || 'Pending', badge: 'badge-blue' };
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

  // ── Chain Rates & SPV ──

  async function loadChainRates() {
    try {
      const data = await App.daemonGet('/getswapprice');
      if (data && data.prices) {
        oraclePrices = data.prices;
        const tbody = document.getElementById('chain-rates');
        if (tbody) {
          tbody.innerHTML = Object.entries(data.prices).map(([chain, info]) => {
            const key = pairKeyFromIndex(chain) || chain;
            const ci = CHAIN_INFO[key] || { icon: '', color: '#888', ticker: chain, name: chain };
            return `<tr>
              <td><img src="${ci.icon || ''}" alt="" style="width:16px;height:16px;border-radius:50%;vertical-align:middle;margin-right:6px;object-fit:contain;">${ci.name} (${ci.ticker})</td>
              <td style="text-align:right;color:var(--firegold-bright);">${info.price != null ? App.fmtPrice(info.price) : '—'}</td>
              <td style="text-align:right;color:var(--text-secondary);">${info.spread != null ? App.fmtPct(info.spread) : '—'}</td>
            </tr>`;
          }).join('');
        }
        if (offers.length) {
          offers.forEach(o => { o.fairPct = fairPctFor(o.pairKey, o.rateXfgPerCtr); });
          renderOrdergraph();
          renderOrderbook();
        }
      }
    } catch {
      // offline
    }
  }

  function loadSPVStatus() {
    App.on('spv_status', updateSPVFromWS);
  }

  function updateSPVFromWS(data) {
    if (!data) return;
    const hEl = document.getElementById('spv-height');
    const vEl = document.getElementById('spv-verified');
    const pEl = document.getElementById('spv-peers');
    const cEl = document.getElementById('spv-chain');
    if (hEl && data.header_height) hEl.textContent = data.header_height.toLocaleString();
    if (vEl && data.verified_txs != null) vEl.textContent = data.verified_txs.toLocaleString();
    if (pEl && data.peer_count != null) pEl.textContent = data.peer_count;
    if (cEl && data.chain) cEl.textContent = data.chain;
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', SwapXFG.init);
