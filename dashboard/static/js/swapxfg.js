// ── SwapXFG Bridge Page ────────────────────────────────────────────────────────
'use strict';

const SwapXFG = (() => {
  let oracleChart, oracleLineSeries;
  let swapDirection = 0; // 0 = XFG→CTR, 1 = CTR→XFG

  const CHAIN_INFO = {
    BTC:      { icon: '/coin-icons/btc.png', color: '#f7931a', ticker: 'BTC', name: 'Bitcoin' },
    ETH:      { icon: '/coin-icons/eth.png', color: '#627eea', ticker: 'ETH', name: 'Ethereum' },
    SOL:      { icon: '/coin-icons/sol.png', color: '#9945ff', ticker: 'SOL', name: 'Solana' },
    XMR:      { icon: '/coin-icons/monero-xmr-logo.png', color: '#ff6600', ticker: 'XMR', name: 'Monero' },
    LTC:      { icon: '/coin-icons/ltc.png', color: '#bfbbbb', ticker: 'LTC', name: 'Litecoin' },
    BCH:      { icon: '/coin-icons/bch.png', color: '#8dc351', ticker: 'BCH', name: 'Bitcoin Cash' },
    ARB:      { icon: '/coin-icons/arb.png', color: '#28a0f0', ticker: 'ARB', name: 'Arbitrum' },
    BASE:     { icon: '/coin-icons/base.png', color: '#0052ff', ticker: 'BASE', name: 'Base' },
    BNB:      { icon: '/coin-icons/bnb.png', color: '#f3ba2f', ticker: 'BNB', name: 'BNB Chain' },
    DCR:      { icon: '/coin-icons/dcr.png', color: '#2970ff', ticker: 'DCR', name: 'Decred' },
    KMD_SPV:  { icon: '/coin-icons/kmd.png', color: '#2b6def', ticker: 'KMD', name: 'Komodo' },
    POLYGON:  { icon: '/coin-icons/matic.png', color: '#8247e5', ticker: 'MATIC', name: 'Polygon' }
  };

  // ── Init ──

  function init() {
    initOracleChart();
    initChainSelect();
    initBridgeForm();
    initSwapDirection();
    loadActiveSwaps();
    loadChainRates();
    loadSPVStatus();

    // WebSocket: live swap updates
    App.on('swap_update', updateSwapFromWS);
    App.on('spv_status', updateSPVFromWS);
    App.on('block', () => { loadActiveSwaps(); loadChainRates(); });

    // Periodic refresh
    setInterval(loadActiveSwaps, 10000);
    setInterval(loadChainRates, 30000);
  }

  // ── Oracle Chart ──

  function initOracleChart() {
    const container = document.getElementById('oracle-chart');
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

    // Load historical oracle data
    loadOracleHistory();
  }

  async function loadOracleHistory() {
    try {
      const candles = await App.rpc('get_ohlvc', { timeframe: '1h', count: 168 });
      if (candles && candles.candles) {
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
      iconImg.style.borderRadius = '50%';
      iconImg.style.width = '32px';
      iconImg.style.height = '32px';
      document.getElementById('to-chain-name').textContent = info.name;
      document.getElementById('to-chain-ticker').textContent = info.ticker;
    });
    // Trigger initial
    select.dispatchEvent(new Event('change'));
  }

  // ── Bridge Form ──

  function initBridgeForm() {
    const amountInput = document.getElementById('bridge-amount');
    amountInput.addEventListener('input', updateEstimate);

    // Initiate swap button
    document.getElementById('bridge-init-btn').addEventListener('click', showInitiateModal);

    // Modal close button
    document.getElementById('init-modal-close').addEventListener('click', () => {
      document.getElementById('init-modal').classList.remove('active');
    });

    // Execute via wallet RPC (keys never touch browser)
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
        loadActiveSwaps();
      } catch (e) {
        App.showToast(`Error: ${e.message}`);
      }
    });

    // Copy CLI command
    document.getElementById('init-copy-btn').addEventListener('click', () => {
      const cmd = document.getElementById('init-cli-cmd');
      App.copyToClipboard(cmd.textContent);
    });
  }

  function updateEstimate() {
    const amount = parseFloat(document.getElementById('bridge-amount').value) || 0;
    const chain = document.getElementById('to-chain-select').value;
    const fee = amount * 0.02; // 2% swap fee
    const output = amount - fee;
    document.getElementById('bridge-estimated').textContent = output > 0 ? output.toFixed(4) : '0.00';
    document.getElementById('bridge-rate').textContent = amount > 0
      ? `1 XFG ≈ ${(output / amount).toFixed(4)} ${CHAIN_INFO[chain].ticker} (after fees)`
      : 'Select an amount to see the rate';
  }

  function initSwapDirection() {
    document.getElementById('bridge-swap-dir').addEventListener('click', () => {
      swapDirection = swapDirection === 0 ? 1 : 0;
      const btn = document.getElementById('bridge-swap-dir');
      btn.textContent = swapDirection === 0 ? '↓' : '↑';

      if (swapDirection === 1) {
        document.getElementById('from-group').querySelector('.bridge-chain-icon').textContent = '?';
        document.getElementById('from-group').querySelector('.bridge-chain-icon').style.color = 'var(--blue)';
        document.getElementById('from-group').querySelector('.bridge-chain-icon').id = 'from-chain-icon-dynamic';
      } else {
        document.getElementById('from-group').querySelector('.bridge-chain-icon').textContent = 'X';
        document.getElementById('from-group').querySelector('.bridge-chain-icon').style.color = 'var(--accent)';
      }
    });
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

    document.getElementById('init-modal').classList.add('active');
  }

  // ── Active Swaps (Package Tracker) ──

  async function loadActiveSwaps() {
    try {
      const data = await App.daemonGet('/api/swapd/');
      const swaps = data.active_swaps || data.swaps || [];
      renderSwaps(swaps);
    } catch {
      // swapd might not be running
    }
  }

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
      const info = CHAIN_INFO[s.pair] || { icon: '?', color: '#888', ticker: s.pair, name: s.pair };
      const state = getSwapStateInfo(s.state);
      const steps = getSwapSteps(s.state);

      return `
        <div style="padding:16px;border-bottom:1px solid var(--border);">
          <div style="display:flex;align-items:center;gap:12px;margin-bottom:12px;">
            <div class="bridge-chain-icon" style="color:${info.color};width:32px;height:32px;font-size:16px;">${info.icon}</div>
            <div style="flex:1;">
              <div style="font-weight:600;font-size:14px;">${App.fmtXfg(s.xfg_amount)} XFG → ${info.ticker}</div>
              <div style="font-size:12px;color:var(--text-muted);">${s.swap_id ? s.swap_id.substring(0, 16) : '—'}</div>
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
      if (stepDefs[i].states.includes(state)) {
        reached = i;
        break;
      }
    }
    if (terminalOk.includes(state)) reached = stepDefs.length - 1;

    return stepDefs.map((s, i) => ({
      ...s,
      status: i < reached ? 'done' : i === reached ? (terminalOk.includes(state) ? 'done' : 'active') : ''
    }));
  }

  function updateSwapFromWS(data) {
    if (data && data.active_swaps) {
      renderSwaps(data.active_swaps);
    }
  }

  // ── Chain Rates ──

  async function loadChainRates() {
    try {
      const data = await App.daemonGet('/getswapprice');
      if (data && data.prices) {
        const tbody = document.getElementById('chain-rates');
        tbody.innerHTML = Object.entries(data.prices).map(([chain, info]) => {
          const ci = CHAIN_INFO[chain] || { icon: '?', color: '#888', ticker: chain };
          return `<tr>
            <td><span style="color:${ci.color};margin-right:6px;">${ci.icon}</span>${ci.ticker}</td>
            <td style="text-align:right">${info.price ? App.fmtPrice(info.price) : '—'}</td>
            <td style="text-align:right">${info.spread ? App.fmtPct(info.spread) : '—'}</td>
          </tr>`;
        }).join('');
      }
    } catch {
      // oracle might not be available
    }
  }

  // ── SPV Status ──

  function loadSPVStatus() {
    // SPV data comes from swap_update events
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
