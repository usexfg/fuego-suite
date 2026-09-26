// ── DeXFG Ordergraph + Orderbook ─────────────────────────────────────────────
'use strict';

const SwapXFG = (() => {
  let oracleChart, oracleLineSeries;
  let swapDirection = 0; // 0 = XFG→CTR, 1 = CTR→XFG
  let offers = [];
  let selectedOfferId = null;
  let chainHeight = 0;
  let oraclePrices = {}; // chainKey -> { price, spread } or rate

  // Populated exclusively from xfg-swapd's compile-time catalog. Numeric pair
  // IDs are protocol data, so the dashboard must never maintain a second list.
  const PAIR_BY_INDEX = [];
  const CHAIN_INFO = {};
  let catalogFingerprint = '';

  const FUEGO_ICON = '/coin-icons/fuego.png';
  const UINT64_MAX = 18446744073709551615n;
  const UINT256_MAX = (1n << 256n) - 1n;

  /** Stable alphabetical protocol columns by display name. */
  function orderedChains() {
    return Object.keys(CHAIN_INFO).filter(k => CHAIN_INFO[k].protocol).sort((a, b) =>
      CHAIN_INFO[a].name.localeCompare(CHAIN_INFO[b].name)
    );
  }

  function allCatalogChains() {
    return Object.keys(CHAIN_INFO).sort((a, b) =>
      CHAIN_INFO[a].name.localeCompare(CHAIN_INFO[b].name)
    );
  }

  function applyChainCatalog(rawChains) {
    if (!Array.isArray(rawChains) || rawChains.length === 0) return false;
    const fingerprint = JSON.stringify(rawChains);
    if (fingerprint === catalogFingerprint) return false;

    const nextByIndex = [];
    const nextInfo = {};
    rawChains.forEach(raw => {
      const id = Number(raw.id);
      const symbol = String(raw.symbol || '').toUpperCase();
      if (!Number.isInteger(id) || id < 0 || id > 255 || !/^[A-Z0-9_]+$/.test(symbol)) return;
      const iconName = /^[a-zA-Z0-9._-]+$/.test(String(raw.icon || ''))
        ? String(raw.icon)
        : 'fuego.png';
      const color = /^#[0-9a-fA-F]{6}$/.test(String(raw.color || ''))
        ? String(raw.color)
        : '#888888';
      nextByIndex[id] = symbol;
      nextInfo[symbol] = {
        id,
        configKey: String(raw.key || ''),
        icon: `/coin-icons/${iconName}`,
        color,
        ticker: String(raw.assetTicker || symbol),
        name: String(raw.name || symbol),
        family: String(raw.family || 'unknown'),
        chainId: Number(raw.chainId || 0),
        decimals: Number(raw.decimals || 0),
        implementation: String(raw.implementation || 'staged'),
        protocol: raw.protocol === true,
        configured: raw.configured === true,
        ready: raw.ready === true,
        readinessError: String(raw.readinessError || '')
      };
    });

    PAIR_BY_INDEX.length = 0;
    nextByIndex.forEach((symbol, id) => { PAIR_BY_INDEX[id] = symbol; });
    Object.keys(CHAIN_INFO).forEach(key => { delete CHAIN_INFO[key]; });
    Object.assign(CHAIN_INFO, nextInfo);
    catalogFingerprint = fingerprint;
    rebuildChainSelect();
    initOrdergraphShell();
    return true;
  }

  function pairKeyFromIndex(idx) {
    if (typeof idx === 'string' && CHAIN_INFO[idx.toUpperCase()]) return idx.toUpperCase();
    const n = Number(idx);
    if (!Number.isNaN(n) && PAIR_BY_INDEX[n]) return PAIR_BY_INDEX[n];
    return null;
  }

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function escapeAttr(value) {
    return escapeHtml(value);
  }

  function parseAtomicText(value, label, max = UINT64_MAX) {
    const text = String(value || '').trim();
    if (!/^[0-9]+$/.test(text)) throw new Error(`${label} must be a positive integer`);
    const amount = BigInt(text);
    if (amount <= 0n || amount > max) {
      throw new Error(`${label} is outside the supported atomic-unit range`);
    }
    return amount;
  }

  function parseStatusUint64(value) {
    if (typeof value === 'bigint') return value >= 0n && value <= UINT64_MAX ? value : 0n;
    if (typeof value === 'number') {
      return Number.isSafeInteger(value) && value >= 0 ? BigInt(value) : 0n;
    }
    const text = String(value ?? '').trim();
    if (!/^[0-9]+$/.test(text)) return 0n;
    const parsed = BigInt(text);
    return parsed <= UINT64_MAX ? parsed : 0n;
  }

  function decimalToAtomic(value, decimals, label) {
    const text = String(value || '').trim();
    if (!/^[0-9]+(?:\.[0-9]+)?$/.test(text)) throw new Error(`${label} must be a positive decimal`);
    const [whole, fraction = ''] = text.split('.');
    if (fraction.length > decimals && /[1-9]/.test(fraction.slice(decimals))) {
      throw new Error(`${label} supports at most ${decimals} decimal places`);
    }
    const atomicText = `${whole}${fraction.slice(0, decimals).padEnd(decimals, '0')}`.replace(/^0+(?=\d)/, '');
    return parseAtomicText(atomicText, label);
  }

  function shellQuote(value) {
    return `'${String(value).replace(/'/g, `'\\''`)}'`;
  }

  function normalizeOffer(raw) {
    const pairKey = pairKeyFromIndex(raw.pair);
    const xfgAmountAtomic = parseStatusUint64(
      raw.xfgAmountAtomic ?? raw.xfgAmount ?? raw.xfg_amount ?? 0
    );
    const filledAmountAtomic = parseStatusUint64(
      raw.filledAmountAtomic ?? raw.filledAmount ?? raw.filled_amount ?? 0
    );
    const remainingAtomic = filledAmountAtomic < xfgAmountAtomic
      ? xfgAmountAtomic - filledAmountAtomic : 0n;
    const rateNumAtomic = parseStatusUint64(
      raw.rateNumAtomic ?? raw.rateNum ?? raw.rate_num ?? 0
    );
    // Numbers below are presentation-only approximations. Execution always
    // uses the exact BigInt/string values above.
    const xfgAmount = Number(xfgAmountAtomic);
    const filledAmount = Number(filledAmountAtomic);
    const remaining = Number(remainingAtomic);
    const rateNum = Number(rateNumAtomic);
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
      xfgAmountAtomic,
      filledAmountAtomic,
      remainingAtomic,
      rateNumAtomic,
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
    const wrap = document.querySelector('.ordergraph-wrap');
    const graphWidth = Math.max(720, chains.length * 72);
    if (wrap) wrap.style.minWidth = `${graphWidth + 58}px`;
    axis.style.minWidth = `${graphWidth}px`;

    if (chains.length === 0) {
      cols.style.gridTemplateColumns = '1fr';
      cols.innerHTML = '<div class="empty-state-text" style="padding:24px;">Waiting for xfg-swapd chain catalog…</div>';
      axis.innerHTML = '';
      return;
    }
    cols.style.gridTemplateColumns = `repeat(${chains.length}, minmax(36px, 1fr))`;
    axis.style.gridTemplateColumns = `repeat(${chains.length}, minmax(36px, 1fr))`;

    cols.innerHTML = chains.map(k =>
      `<div class="ordergraph-col" data-chain="${k}"></div>`
    ).join('');

    axis.innerHTML = chains.map(k => {
      const c = CHAIN_INFO[k];
      return `<div class="ordergraph-axis-cell empty" data-chain="${k}">
        <div class="ordergraph-depth"><i style="width:0%;background:${c.color}"></i></div>
        <div class="chain-name">${escapeHtml(c.name)}</div>
        <div class="chain-ticker">${escapeHtml(c.ticker)}</div>
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
    select.addEventListener('change', updateSelectedChain);
    updateSelectedChain();
  }

  function rebuildChainSelect() {
    const select = document.getElementById('to-chain-select');
    if (!select) return;
    const previous = select.value;
    select.innerHTML = '';

    let firstReady = '';
    allCatalogChains().forEach(chain => {
      const info = CHAIN_INFO[chain];
      const option = document.createElement('option');
      option.value = chain;
      let suffix = '';
      if (info.implementation === 'staged') suffix = ' — staged';
      else if (!info.ready) suffix = ' — setup required';
      option.textContent = `${info.name} (${info.ticker})${suffix}`;
      option.disabled = !info.ready;
      option.title = info.readinessError || (info.ready ? 'Ready' : 'Not ready for new swaps');
      if (info.ready && !firstReady) firstReady = chain;
      select.appendChild(option);
    });

    const keepPrevious = previous && CHAIN_INFO[previous]?.ready;
    if (keepPrevious) {
      select.value = previous;
    } else if (firstReady) {
      select.value = firstReady;
    } else {
      const placeholder = document.createElement('option');
      placeholder.value = '';
      placeholder.textContent = 'No locally ready chains';
      placeholder.disabled = true;
      placeholder.selected = true;
      select.prepend(placeholder);
    }
    updateSelectedChain();
  }

  function fallbackIconData(label, color) {
    const initials = String(label || '?').replace(/[^a-zA-Z0-9]/g, '').slice(0, 3).toUpperCase() || '?';
    const fill = /^#[0-9a-fA-F]{6}$/.test(String(color || '')) ? color : '#555566';
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><circle cx="32" cy="32" r="31" fill="${fill}"/><text x="32" y="38" text-anchor="middle" font-family="sans-serif" font-size="18" font-weight="700" fill="white">${initials}</text></svg>`;
    return `data:image/svg+xml,${encodeURIComponent(svg)}`;
  }

  function setImageSource(img, src, alt, color = '#555566') {
    img.onerror = () => {
      img.onerror = null;
      img.src = fallbackIconData(alt, color);
    };
    img.src = src || FUEGO_ICON;
    img.alt = alt || '';
  }

  function bindImageFallbacks(root) {
    root.querySelectorAll('img[data-fallback-label]').forEach(img => {
      const label = img.dataset.fallbackLabel || '?';
      const color = img.dataset.fallbackColor || '#555566';
      img.onerror = () => {
        img.onerror = null;
        img.src = fallbackIconData(label, color);
      };
    });
  }

  function updateSelectedChain() {
    const select = document.getElementById('to-chain-select');
    const info = CHAIN_INFO[select.value];
    const button = document.getElementById('bridge-init-btn');
    if (!info) {
      setImageSource(document.getElementById('to-chain-icon'), FUEGO_ICON, '');
      document.getElementById('to-chain-name').textContent = 'No chain ready';
      document.getElementById('to-chain-ticker').textContent = 'Configure xfg-swapd';
      button.disabled = true;
      updateEstimate();
      return;
    }
    setImageSource(document.getElementById('to-chain-icon'), info.icon, info.ticker, info.color);
    document.getElementById('to-chain-name').textContent = info.name;
    document.getElementById('to-chain-ticker').textContent = info.ready
      ? info.ticker
      : `${info.ticker} · ${info.implementation === 'staged' ? 'staged' : 'setup required'}`;
    button.disabled = !info.ready;
    button.title = info.ready ? '' : (info.readinessError || 'Chain is not ready for new swaps');
    updateEstimate();
  }

  // ── Bridge Form ──

  function initBridgeForm() {
    document.getElementById('bridge-amount').addEventListener('input', updateEstimate);
    document.getElementById('ctr-amount').addEventListener('input', updateEstimate);
    document.getElementById('bridge-init-btn').addEventListener('click', showInitiateModal);
    document.getElementById('init-modal-close').addEventListener('click', () => {
      document.getElementById('init-modal').classList.remove('active');
    });

    document.getElementById('init-exec-btn').addEventListener('click', async () => {
      try {
        const request = collectInitiationRequest();
        const result = await App.swapRpc('initiate_swap', {
          pair: request.chain,
          xfg_amount: request.xfgAtomic.toString(),
          ctr_amount: request.ctrAtomic.toString(),
          peer: request.peer,
          role: request.role,
          expected_peer_pubkey: request.peerKey
        });
        const swapId = result?.swap_id || '';
        App.showToast(`Swap initiated! ID: ${swapId ? swapId.substring(0, 16) : 'pending'}`);
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

  function formatAtomicUnits(amount, decimals) {
    const value = BigInt(amount);
    if (decimals <= 0) return value.toString();
    const scale = 10n ** BigInt(decimals);
    const whole = value / scale;
    const fraction = (value % scale).toString().padStart(decimals, '0').replace(/0+$/, '');
    const clipped = fraction.slice(0, 8);
    return clipped ? `${whole}.${clipped}` : whole.toString();
  }

  function collectInitiationRequest() {
    const chain = document.getElementById('to-chain-select').value;
    const info = CHAIN_INFO[chain];
    if (!info) throw new Error('Select a chain');
    if (!info.ready) throw new Error(info.readinessError || `${info.name} is not ready for new swaps`);

    const xfgAtomic = decimalToAtomic(
      document.getElementById('bridge-amount').value, 7, 'XFG amount'
    );
    const ctrAtomic = parseAtomicText(
      document.getElementById('ctr-amount').value, `${info.ticker} amount`,
      info.family === 'evm' ? UINT256_MAX : UINT64_MAX
    );
    const peer = document.getElementById('peer-endpoint').value.trim();
    if (!peer) throw new Error('Peer swap endpoint is required');
    const peerKey = document.getElementById('peer-pubkey').value.trim();
    if (peerKey && !/^[0-9a-fA-F]{64}$/.test(peerKey)) {
      throw new Error('Expected peer public key must be 64 hexadecimal characters');
    }
    return {
      chain, info, xfgAtomic, ctrAtomic, peer, peerKey,
      role: swapDirection === 0 ? 'bob' : 'alice'
    };
  }

  function updateEstimate() {
    const chain = document.getElementById('to-chain-select').value;
    const info = CHAIN_INFO[chain];
    const output = document.getElementById('bridge-estimated');
    const rate = document.getElementById('bridge-rate');
    if (!info) {
      output.textContent = '—';
      rate.textContent = 'Waiting for a ready xfg-swapd chain';
      return;
    }

    try {
      const ctrAtomic = parseAtomicText(document.getElementById('ctr-amount').value,
        info.ticker, info.family === 'evm' ? UINT256_MAX : UINT64_MAX);
      output.textContent = `${formatAtomicUnits(ctrAtomic, info.decimals)} ${info.ticker}`;
    } catch {
      output.textContent = '—';
    }

    const selected = offers.find(o => o.offerId === selectedOfferId && o.pairKey === chain);
    rate.textContent = selected && selected.rateXfgPerCtr > 0
      ? `Selected offer: 1 ${info.ticker} = ${selected.rateXfgPerCtr.toFixed(7)} XFG`
      : 'Enter the exact amount agreed with your counterparty';
  }

  function initSwapDirection() {
    document.getElementById('bridge-swap-dir').addEventListener('click', () => {
      swapDirection = swapDirection === 0 ? 1 : 0;
      updateSwapDirectionUI();
    });
    updateSwapDirectionUI();
  }

  function updateSwapDirectionUI() {
    const button = document.getElementById('bridge-swap-dir');
    button.textContent = swapDirection === 0 ? '↓' : '↑';
    button.title = swapDirection === 0
      ? 'Our side funds XFG (Bob role)'
      : 'Our side funds the counterparty asset (Alice role)';
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
    applyChainCatalog(data.chains);
    if (data.height != null) chainHeight = Number(data.height) || chainHeight;

    const rawOffers = data.offers || [];
    offers = rawOffers.map(normalizeOffer).filter(o =>
      o.pairKey && CHAIN_INFO[o.pairKey]?.ready && o.remaining > 0);

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
        setImageSource(img, logo, o.isSell ? 'XFG' : info.ticker, o.isSell ? '#ff6b35' : info.color);
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

  function showTooltip(e, o) {
    const tip = document.getElementById('ordergraph-tooltip');
    const info = CHAIN_INFO[o.pairKey] || {};
    const side = o.isSell ? 'Sell XFG → ' + (info.ticker || o.pairKey) : 'Buy XFG ← ' + (info.ticker || o.pairKey);
    const fair = (o.fairPct >= 0 ? '+' : '') + o.fairPct.toFixed(2) + '% vs fair';
    const exp = o.blocksLeft != null ? `~${o.blocksLeft} blocks left` : 'expiry n/a';
    tip.innerHTML = `<strong>${escapeHtml(side)}</strong><br>` +
      `${escapeHtml(formatAtomicUnits(o.remainingAtomic, 7))} XFG remaining<br>` +
      `rate ${escapeHtml(formatAtomicUnits(o.rateNumAtomic, 7))} XFG/${escapeHtml(info.ticker || '?')} · ${escapeHtml(fair)}<br>` +
      `${escapeHtml(exp)}${o.isSoftOrder ? ' · soft' : ''}`;
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
                <img src="${escapeAttr(logo)}" alt="" data-fallback-label="${escapeAttr(o.isSell ? 'XFG' : info.ticker)}" data-fallback-color="${info.color}">
              </div>
              <div>
                <div class="ob-title">${escapeHtml(side)} · ${escapeHtml(info.name)}</div>
                <div class="ob-sub">${escapeHtml(idShort)}${o.isSoftOrder ? ' · soft order' : ''}</div>
              </div>
              <span class="badge ${o.isSell ? 'badge-yellow' : 'badge-blue'}">${o.isSell ? 'SELL XFG' : 'BUY XFG'}</span>
            </div>
            <div class="ob-grid">
              <div><span class="k">Remaining</span><span class="v">${formatAtomicUnits(o.remainingAtomic, 7)} XFG</span></div>
              <div><span class="k">Original</span><span class="v">${formatAtomicUnits(o.xfgAmountAtomic, 7)} XFG</span></div>
              <div><span class="k">Filled</span><span class="v">${formatAtomicUnits(o.filledAmountAtomic, 7)} XFG</span></div>
              <div><span class="k">Rate</span><span class="v">${formatAtomicUnits(o.rateNumAtomic, 7)} / ${escapeHtml(info.ticker)}</span></div>
              <div><span class="k">vs Fair</span><span class="v">${fairStr}</span></div>
              <div><span class="k">Expiry</span><span class="v">${o.blocksLeft != null ? o.blocksLeft + ' blks' : '—'}</span></div>
              <div><span class="k">Posted</span><span class="v">${o.postedHeight ? '#' + o.postedHeight : '—'}</span></div>
            </div>
          </div>
          <div class="ob-actions">
            <button type="button" class="btn btn-primary" data-act="accept" data-id="${escapeAttr(o.offerId)}">Prepare take</button>
            <button type="button" class="btn btn-secondary" data-act="fill" data-id="${escapeAttr(o.offerId)}">Fill form</button>
            <button type="button" class="btn btn-secondary" data-act="copy" data-id="${escapeAttr(o.offerId)}">Copy ID</button>
          </div>
        </div>`;
    }).join('');
    bindImageFallbacks(list);

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
    el.textContent = `Selected: ${o.isSell ? 'Sell' : 'Buy'} · ${formatAtomicUnits(o.remainingAtomic, 7)} XFG · ${info.name || o.pairKey} · ${o.offerId.substring(0, 20)}…`;
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
    document.getElementById('bridge-amount').value = formatAtomicUnits(o.remainingAtomic, 7);
    const info = CHAIN_INFO[o.pairKey];
    document.getElementById('ctr-amount').value = '';
    if (info && o.rateNumAtomic > 0n && o.remainingAtomic > 0n) {
      const ctrAtomic = (o.remainingAtomic * (10n ** BigInt(info.decimals))) / o.rateNumAtomic;
      if (ctrAtomic > 0n && ctrAtomic <= (info.family === 'evm' ? UINT256_MAX : UINT64_MAX)) {
        document.getElementById('ctr-amount').value = ctrAtomic.toString();
      }
    }
    swapDirection = o.isSell ? 1 : 0;
    updateSwapDirectionUI();
    updateEstimate();
    App.showToast('Form filled from offer');
  }

  function acceptOffer(offerId) {
    const o = offers.find(x => x.offerId === offerId);
    if (!o) {
      App.showToast('Offer not found');
      return;
    }
    selectOffer(offerId, true);
    fillFormFromOffer(offerId);
    App.showToast('Offer prepared; enter the maker peer endpoint, then review the swap');
  }

  // ── Initiate Modal ──

  function showInitiateModal() {
    let request;
    try {
      request = collectInitiationRequest();
    } catch (e) {
      App.showToast(e.message);
      return;
    }
    const amount = document.getElementById('bridge-amount').value;
    const ctrDisplay = formatAtomicUnits(request.ctrAtomic, request.info.decimals);

    document.getElementById('init-modal-details').innerHTML = `
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;">
        <div class="metric"><span class="metric-label">XFG amount</span><span class="metric-value accent">${escapeHtml(amount)} XFG</span></div>
        <div class="metric"><span class="metric-label">Counterparty amount</span><span class="metric-value" style="color:${request.info.color}">${escapeHtml(ctrDisplay)} ${escapeHtml(request.info.ticker)}</span></div>
        <div class="metric"><span class="metric-label">Chain</span><span class="metric-value">${escapeHtml(request.info.name)}</span></div>
        <div class="metric"><span class="metric-label">Our role</span><span class="metric-value">${request.role === 'bob' ? 'Fund XFG' : 'Fund counterparty asset'}</span></div>
        <div class="metric" style="grid-column:1 / -1"><span class="metric-label">Peer endpoint</span><span class="metric-value">${escapeHtml(request.peer)}</span></div>
      </div>`;

    const rpcParams = {
      pair: request.chain,
      xfg_amount: request.xfgAtomic.toString(),
      ctr_amount: request.ctrAtomic.toString(),
      peer: request.peer,
      role: request.role,
      expected_peer_pubkey: request.peerKey
    };
    const rpcBody = JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initiate_swap', params: rpcParams });
    document.getElementById('init-cli-cmd').textContent =
      `curl --fail-with-body -sS -H 'Content-Type: application/json' --data ${shellQuote(rpcBody)} ${shellQuote(`${location.origin}/api/swapd-rpc`)}`;
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
            <img src="${escapeAttr(info.icon)}" alt="" data-fallback-label="${escapeAttr(info.ticker)}" data-fallback-color="${info.color}" style="width:32px;height:32px;border-radius:50%;object-fit:contain;background:${info.color}33;">
            <div style="flex:1;">
              <div style="font-weight:600;font-size:14px;">XFG ↔ ${escapeHtml(info.ticker)}</div>
              <div style="font-size:12px;color:var(--text-muted);font-family:var(--font-mono);">${escapeHtml(sid ? sid.substring(0, 16) + '…' : '—')}</div>
            </div>
            <span class="badge ${state.badge}">${escapeHtml(state.label)}</span>
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
          ${s.error ? `<div style="margin-top:8px;font-size:12px;color:var(--red);">${escapeHtml(s.error)}</div>` : ''}
        </div>`;
    }).join('');
    bindImageFallbacks(list);
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
            <td><img src="${ci.icon || ''}" alt="" data-fallback-label="${escapeAttr(ci.ticker)}" data-fallback-color="${ci.color || '#888888'}" style="width:16px;height:16px;border-radius:50%;vertical-align:middle;margin-right:6px;object-fit:contain;">${escapeHtml(ci.ticker)}</td>
            <td style="text-align:right">${info.price != null ? App.fmtPrice(info.price) : '—'}</td>
            <td style="text-align:right">${info.spread != null ? App.fmtPct(info.spread) : '—'}</td>
          </tr>`;
        }).join('');
        bindImageFallbacks(tbody);
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
