// ── Hearth — Atelier Bench ───────────────────────────────────────────────────
// A demonstration surface. Every figure on this page is synthesised locally;
// no daemon, wallet, or transfer process is contacted, and nothing here can
// reach a vault. The house economics (1% salon fee, 69/11/20 distribution,
// the $1.58 reference) are the real protocol constants, so the bench is
// faithful even though the depth is not real.
//
// Deliberately independent of app.js and hearth.js: the bench may be reworked
// without touching the live surface, and vice versa.
'use strict';

// ── Register ──────────────────────────────────────────────────────────────────
// The bench carries its own theme module rather than reaching for the shared
// app layer, so it stays a genuinely independent copy. Behaviour is identical:
// three registers, persisted, resolved before first paint, announced to the
// chart so the plot follows without a reload.
const THEMES = ['maison', 'fulltrade', 'reference'];
const THEME_KEY = 'xfg.theme';
let theme = 'reference';

const tokens = names => {
  const cs = getComputedStyle(document.documentElement);
  const out = {};
  names.forEach(n => { out[n] = cs.getPropertyValue(n).trim(); });
  return out;
};

const themeListeners = [];
const onTheme = fn => themeListeners.push(fn);

function applyTheme(name, persist) {
  if (!THEMES.includes(name)) return;
  theme = name;
  document.documentElement.setAttribute('data-theme', name);
  if (persist !== false) { try { localStorage.setItem(THEME_KEY, name); } catch (e) { /* private mode */ } }
  document.querySelectorAll('.theme-opt').forEach(b => {
    b.setAttribute('aria-pressed', String(b.dataset.theme === theme));
  });
  themeListeners.forEach(fn => { try { fn(theme); } catch (e) { console.warn('[bench] theme listener:', e); } });
}

function initThemeSwitcher() {
  let stored = null;
  try { stored = localStorage.getItem(THEME_KEY); } catch (e) { /* private mode */ }
  applyTheme(THEMES.includes(stored) ? stored : theme, false);
  document.querySelectorAll('.theme-opt').forEach(btn => {
    btn.addEventListener('click', () => applyTheme(btn.dataset.theme, true));
  });
}

// ── Chart palette, resolved from the bench's own stylesheet ──────────────────
const CHART_TOKENS = [
  '--dir-up', '--dir-up-line', '--dir-down', '--dir-down-line',
  '--ink-30', '--ink-20', '--ink-50', '--ink-90',
  '--hairline', '--hairline-soft',
  '--accent', '--accent-line', '--flame',
  '--surface-active', '--surface-panel'
];

const ChartStyle = (() => {
  const build = t => ({
    grid: { show: true, horizontal: { color: t['--hairline-soft'] }, vertical: { color: t['--hairline-soft'] } },
    candle: {
      type: 'candle_solid',
      bar: {
        upColor: t['--dir-up'], downColor: t['--dir-down'], noChangeColor: t['--ink-30'],
        upBorderColor: t['--dir-up'], downBorderColor: t['--dir-down'], noChangeBorderColor: t['--ink-30'],
        upWickColor: t['--dir-up-line'], downWickColor: t['--dir-down-line'], noChangeWickColor: t['--ink-20']
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
      lines: [{ color: t['--accent'], size: 1 }, { color: t['--ink-50'], size: 1 }]
    },
    xAxis: { axisLine: { color: t['--hairline'] }, tickLine: { color: t['--hairline-soft'] }, tickText: { color: t['--ink-30'], size: 10 } },
    yAxis: { axisLine: { color: t['--hairline'] }, tickLine: { color: t['--hairline-soft'] }, tickText: { color: t['--ink-30'], size: 10 } },
    separator: { color: t['--hairline'] },
    crosshair: {
      horizontal: { line: { color: t['--accent-line'], style: 1 }, text: { color: t['--ink-90'], backgroundColor: t['--surface-active'] } },
      vertical: { line: { color: t['--accent-line'], style: 1 }, text: { color: t['--ink-90'], backgroundColor: t['--surface-active'] } }
    }
  });
  const overlay = () => {
    const t = tokens(CHART_TOKENS);
    const s = { line: { color: t['--accent'], style: 0, size: 1 }, text: { color: t['--ink-50'], size: 10 } };
    return {
      segment: { name: 'segment', styles: s },
      horizontalRay: { name: 'priceLine', styles: { ...s, line: { color: t['--accent'], style: 2, size: 1 } } },
      fibonacci: { name: 'fibonacciLine', styles: { ...s, polyline: { color: t['--accent-line'], style: 2, size: 1 } } },
      rectangle: { name: 'rect', styles: { ...s, polygon: { color: t['--accent'] + '18', borderColor: t['--accent-line'] } } }
    };
  };
  return { build, overlay };
})();

// ── Formatting helpers (bench-local, no shared app layer) ─────────────────────
const Bench = (() => {
  const COIN = 1e7;

  const xfg = a => a == null ? '—' : (a / COIN).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const heat = a => a == null ? '—' : (a / COIN).toLocaleString(undefined, { minimumFractionDigits: 0, maximumFractionDigits: 0 });
  const price = a => a == null ? '—' : (a / COIN).toFixed(4);

  function toast(msg) {
    let el = document.getElementById('app-toast');
    if (!el) {
      el = document.createElement('div');
      el.id = 'app-toast';
      document.body.appendChild(el);
    }
    el.textContent = msg;
    el.style.opacity = '1';
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { el.style.opacity = '0'; }, 2000);
  }
  return { COIN, xfg, heat, price, toast };
})();

// ── Deterministic noise ──────────────────────────────────────────────────────
// A fixed seed keeps the bench identical on every load, so a change in the
// layout can be attributed to the change and not to fresh random figures.
function mulberry32(seed) {
  return function () {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ── Demonstration state ──────────────────────────────────────────────────────
// One object drives every surface on the page, so the chart, the ladders and
// the register always tell the same story.
const Demo = (() => {
  const rand = mulberry32(20260928);

  // Volatility clusters, the way a real series behaves: quiet stretches and
  // active ones, rather than uniform noise.
  let vol = 0.0016;
  let spot = 158 * Bench.COIN;

  const state = {
    // Reserves. spot is derived from these, so the register and the pool agree.
    reserveXfg: 125_000 * Bench.COIN,
    reserveHeat: 19_750_000 * Bench.COIN,
    totalLpShares: 42_000,
    accumulatedLpFees: 3_200 * Bench.COIN,
    epochSwapFees: 180 * Bench.COIN,

    // House-wide figures.
    xfgTransformed: 1_200_000 * Bench.COIN,
    heatCirculating: 8_500_000 * Bench.COIN,
    cdPool: 45_000 * Bench.COIN,

    get spot() { return spot; },
    get heatPerXfg() { return state.reserveHeat / state.reserveXfg; }
  };

  // A slow random walk in log space, with occasional regime shifts.
  function step() {
    if (rand() < 0.04) vol = 0.0008 + rand() * 0.0042;   // regime change
    const drift = (rand() - 0.497) * vol;
    spot = Math.max(140 * Bench.COIN, Math.min(178 * Bench.COIN, spot * (1 + drift)));

    // Reserves follow the price, holding the product ~constant as a real pool does.
    const k = (state.reserveXfg * state.reserveHeat) / spot;
    state.reserveXfg = Math.sqrt(k * state.reserveHeat);
    state.reserveHeat = Math.sqrt(k * state.reserveXfg);
    return spot;
  }

  // ── Series ─────────────────────────────────────────────────────────────────
  // Built once, then extended live. Wicks are sized from the same volatility
  // that drives the close, so the bars look like a market rather than noise.
  function buildSeries(periodSec, count) {
    const bars = [];
    const now = Math.floor(Date.now() / 1000);
    const t0 = now - count * periodSec;
    let p = 150 * Bench.COIN;
    for (let i = 0; i < count; i++) {
      const v = 0.006 + rand() * 0.011;
      const open = p;
      const close = Math.max(138 * Bench.COIN, open * (1 + (rand() - 0.492) * v));
      const wick = (0.3 + rand() * 0.7) * v * close;
      const high = Math.max(open, close) + wick * rand();
      const low = Math.min(open, close) - wick * rand();
      const volume = (0.4 + rand() * 2.6) * 4_200 * Bench.COIN;
      bars.push({ t: t0 + i * periodSec, o: open, h: high, l: low, c: close, v: volume });
      p = close;
    }
    // Land the series on the live spot so the chart and the register agree.
    const adj = spot / bars[bars.length - 1].c;
    return bars.map(b => ({
      t: b.t,
      o: b.o * adj, h: b.h * adj, l: b.l * adj, c: b.c * adj, v: b.v
    }));
  }

  // ── Depth ──────────────────────────────────────────────────────────────────
  // A ladder either side of the mid, with a realistic gap and thickness that
  // grows away from the touch.
  function book(levels) {
    const mid = spot;
    const gap = mid * 0.0063;            // ~150 bps
    const step = mid * 0.0018;
    const rand2 = mulberry32(4242);
    const bids = { p: [], a: [], d: [] }, asks = { p: [], a: [], d: [] };
    let cb = 0, ca = 0;
    for (let i = 0; i < levels; i++) {
      const bp = mid - gap / 2 - i * step;
      const ap = mid + gap / 2 + i * step;
      const ba = Math.floor((0.6 + rand2() * 4.2 + i * 0.22) * 100) * Bench.COIN;
      const aa = Math.floor((0.5 + rand2() * 3.8 + i * 0.20) * 100) * Bench.COIN;
      bids.p.push(Math.round(bp)); bids.a.push(ba); cb += ba; bids.d.push(cb);
      asks.p.push(Math.round(ap)); asks.a.push(aa); ca += aa; asks.d.push(ca);
    }
    return { bid_prices: bids.p, bid_amounts: bids.a, bid_depths: bids.d,
             ask_prices: asks.p, ask_amounts: asks.a, ask_depths: asks.d };
  }

  return { state, step, buildSeries, book, rand };
})();

// ── Price archive mount ───────────────────────────────────────────────────────
// All data handling lives in the shared XfgArchive module; this only mounts it.
// The bench is deliberately independent of app.js/hearth.js, but duplicating the
// roll-up and validation in two files would be worse than one pure data module
// both pages read.
const Archive = (() => {
  const PREFIX = 'archive';
  let chart = null;
  let tf = '1d';
  let pending = null;

  function state(msg, isError) {
    const el = document.getElementById(PREFIX + '-state');
    if (!el) return;
    el.textContent = msg;
    el.classList.toggle('error', !!isError);
    el.hidden = !msg;
  }

  function subtitle() {
    const el = document.getElementById(PREFIX + '-sub');
    const s = XfgArchive.span();
    if (!el || !s) return;
    const quote = (XfgArchive.meta && XfgArchive.meta.quote) || 'USD';
    el.textContent = `${XfgArchive.rows.toLocaleString()} daily closes · ${s.first} – `
      + `${s.last} · ${quote}`;
  }

  function apply() {
    if (!chart || !XfgArchive.rows) return;
    chart.applyNewData(XfgArchive.series(tf));
    // A roll-up hides the days behind it, so say so rather than implying the
    // weekly bar is a single observation.
    state(tf === '1w'
      ? 'Weekly roll-up of the daily bars — open is the first daily open, close the last daily close.'
      : null);
  }

  function initChart() {
    const el = document.getElementById(PREFIX + '-chart');
    if (!el || typeof klinecharts === 'undefined') return;
    chart = klinecharts.init(el, { styles: ChartStyle.build(tokens(CHART_TOKENS)) });

    const cross = document.getElementById(PREFIX + '-crosshair');
    chart.subscribeAction('onCrosshairChange', ev => {
      const d = ev && ev.data && ev.data.kLineData;
      if (!d) { if (cross) cross.classList.remove('visible'); return; }
      if (cross) cross.classList.add('visible');
      document.getElementById('ach-o').textContent = 'O $' + XfgArchive.usd(d.open);
      document.getElementById('ach-h').textContent = 'H $' + XfgArchive.usd(d.high);
      document.getElementById('ach-l').textContent = 'L $' + XfgArchive.usd(d.low);
      document.getElementById('ach-c').textContent = 'C $' + XfgArchive.usd(d.close);
      document.getElementById('ach-v').textContent = 'V ' + XfgArchive.vol(d.volume);
      document.getElementById('ach-time').textContent = XfgArchive.day(d.timestamp / 1000);
    });

    try { chart.createIndicator('VOL', false, { id: 'vol_pane' }); } catch (e) { /* pane optional */ }
    window.addEventListener('resize', () => { if (chart) chart.resize(); });
  }

  // Kept separate from rendering so a slow or missing asset never blocks the
  // page it is mounted on.
  function load() {
    state('Loading archive…');
    return XfgArchive.load()
      .then(() => { subtitle(); apply(); state(null); })
      .catch(err => {
        console.warn('[archive] unavailable:', err);
        state('Price archive unavailable.', true);
      });
  }

  function init() {
    initChart();
    document.querySelectorAll('.archive-tf').forEach(b => b.addEventListener('click', () => {
      document.querySelectorAll('.archive-tf').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
      tf = b.dataset.atf;
      apply();
    }));
    pending = load();
  }

  function repaint() {
    if (chart) {
      try { chart.setStyles(ChartStyle.build(tokens(CHART_TOKENS))); }
      catch (e) { console.warn('[archive] repaint failed:', e); }
    }
  }

  return { init, load, repaint, ready: () => pending };
})();

// ── Bench surface ────────────────────────────────────────────────────────────
const HearthTest = (() => {
  let chart;
  let tf = '1h';
  let side = 0;            // 0 = acquire XFG, 1 = release XFG
  let manner = 'limit';    // limit | immediate
  let indicators = { sma20: true, ema12: false, vol: true };
  let drawTool = null;
  let series = [];
  const PERIOD = { '1h': 3600, '4h': 14400, '1d': 86400, '1w': 604800 };
  const LEVELS = 18;

  // ── Chart ──────────────────────────────────────────────────────────────────
  function initChart() {
    const el = document.getElementById('hearth-chart');
    if (!el || typeof klinecharts === 'undefined') return;

    chart = klinecharts.init(el, { styles: ChartStyle.build(tokens(CHART_TOKENS)) });

    const cross = document.getElementById('chart-crosshair');
    chart.subscribeAction('onCrosshairChange', ev => {
      const d = ev && ev.data && ev.data.kLineData;
      if (!d) { cross.classList.remove('visible'); return; }
      cross.classList.add('visible');
      const f = v => (v / Bench.COIN).toFixed(4);
      document.getElementById('ch-o').textContent = 'O ' + f(d.open);
      document.getElementById('ch-h').textContent = 'H ' + f(d.high);
      document.getElementById('ch-l').textContent = 'L ' + f(d.low);
      document.getElementById('ch-c').textContent = 'C ' + f(d.close);
      document.getElementById('ch-v').textContent = 'V ' + (d.volume / 1e6).toFixed(1) + 'M';
      const dt = new Date(d.timestamp);
      document.getElementById('ch-time').textContent = dt.toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
        + ' ' + dt.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    });

    window.addEventListener('resize', () => { if (chart) chart.resize(); });

    document.querySelectorAll('.ind-btn').forEach(b => b.addEventListener('click', () => {
      const k = b.dataset.ind;
      indicators[k] = !indicators[k];
      b.classList.toggle('active', indicators[k]);
      rebuildIndicators();
    }));

    document.querySelectorAll('.draw-btn').forEach(b => b.addEventListener('click', () => {
      const t = b.dataset.tool;
      if (t === 'clear') {
        // klinecharts 9.8 exposes removeOverlay(id), not removeAllOverlay().
        // Walking the completed list is the supported way to clear.
        clearOverlays(chart);
        document.querySelectorAll('.draw-btn').forEach(x => x.classList.remove('active'));
        drawTool = null;
        return;
      }
      if (drawTool === t) { drawTool = null; b.classList.remove('active'); return; }
      drawTool = t;
      document.querySelectorAll('.draw-btn').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
      startDraw(t);
    }));

    rebuildIndicators();
  }

  function rebuildIndicators() {
    if (!chart) return;
    [['candle_pane', 'MA'], ['candle_pane', 'EMA'], ['vol_pane', 'VOL']].forEach(([pane, id]) => {
      try { chart.removeIndicator(pane, id); } catch (e) { /* not present */ }
    });
    if (indicators.sma20) chart.createIndicator('MA', false, { id: 'candle_pane' });
    if (indicators.ema12) chart.createIndicator('EMA', false, { id: 'candle_pane' });
    if (indicators.vol) chart.createIndicator('VOL', false, { id: 'vol_pane' });
  }

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
      console.warn('[bench] clearing overlays failed:', e);
    }
  }

  function startDraw(tool) {    if (!drawTool || !chart) return;
    const cfg = ChartStyle.overlay()[tool];
    if (cfg) {
      chart.createOverlay({
        ...cfg,
        onDrawEnd: () => {
          drawTool = null;
          document.querySelectorAll('.draw-btn').forEach(x => x.classList.remove('active'));
        }
      });
    }
  }

  function applySeries() {
    if (!chart || !series.length) return;
    chart.applyNewData(series.map(b => ({
      timestamp: b.t * 1000, open: b.o / Bench.COIN, high: b.h / Bench.COIN,
      low: b.l / Bench.COIN, close: b.c / Bench.COIN, volume: b.v / Bench.COIN
    })));
  }

  function loadSeries() {
    series = Demo.buildSeries(PERIOD[tf], 200);
    applySeries();
  }

  // ── Depth ladders ──────────────────────────────────────────────────────────
  function renderBook() {
    const bids = document.getElementById('hearth-bids');
    const asks = document.getElementById('hearth-asks');
    if (!bids || !asks) return;

    const d = Demo.book(LEVELS);
    const maxB = Math.max(...d.bid_depths, 1);
    const maxA = Math.max(...d.ask_depths, 1);

    const row = (p, a, cum, max, side) => `
      <div class="ladder-row" data-price="${(p / Bench.COIN).toFixed(4)}" data-amount="${(a / Bench.COIN).toFixed(2)}">
        <span class="price-val">${(p / Bench.COIN).toFixed(4)}</span>
        <span class="amt-val">${(a / Bench.COIN).toFixed(2)}</span>
        <div class="row-bar" style="width:${Math.min(100, Math.round((cum / max) * 100))}%"></div>
      </div>`;

    bids.innerHTML = d.bid_prices.map((p, i) => row(p, d.bid_amounts[i], d.bid_depths[i], maxB, 'bid')).join('');
    // Asks descend toward the touch, the way a ladder is read.
    asks.innerHTML = d.ask_prices.map((p, i) => row(p, d.ask_amounts[i], d.ask_depths[i], maxA, 'ask'))
      .reverse().join('');

    [bids, asks].forEach(box => box.querySelectorAll('.ladder-row').forEach(r => r.addEventListener('click', () => {
      const p = document.getElementById('order-price');
      const a = document.getElementById('order-amount');
      if (p) p.value = r.dataset.price;
      if (a) a.value = r.dataset.amount;
      estimate();
      Bench.toast(`Depth level taken · ${r.dataset.amount} XFG at ${r.dataset.price}`);
    })));
  }

  // ── Register ───────────────────────────────────────────────────────────────
  function renderRegister() {
    const st = Demo.state;
    const set = (id, v) => { const e = document.getElementById(id); if (e) e.textContent = v; };

    set('heat-burned', Bench.xfg(st.xfgTransformed) + ' XFG');
    set('heat-supply', Bench.heat(st.heatCirculating) + ' HΞ∆Ŧ');
    set('pool-xfg', Bench.xfg(st.reserveXfg) + ' XFG');
    set('pool-heat', Bench.heat(st.reserveHeat) + ' HΞ∆Ŧ');
    set('heat-redemption', st.heatPerXfg.toFixed(2) + ' HΞ∆Ŧ / XFG');

    const usd = (st.spot / Bench.COIN) * 1.58;
    set('price-xfg-heat', (st.spot / Bench.COIN).toFixed(4) + ' HΞ∆Ŧ');
    set('price-xfg-usd', '≈ $' + usd.toFixed(2));

    // The quoted spread is the actual gap the ladder is showing, not a caption.
    const b = Demo.book(1);
    const spreadBps = ((b.ask_prices[0] - b.bid_prices[0]) / st.spot) * 10000;
    set('price-spread', 'Spread ' + spreadBps.toFixed(0) + ' bps');

    estimate();
  }

  // ── Ticket ─────────────────────────────────────────────────────────────────
  function initTicket() {
    const buy = document.getElementById('tab-buy');
    const sell = document.getElementById('tab-sell');
    const choose = s => {
      side = s;
      buy.classList.toggle('active', s === 0);
      sell.classList.toggle('active', s === 1);
      label(); estimate();
    };
    buy.addEventListener('click', () => choose(0));
    sell.addEventListener('click', () => choose(1));

    document.querySelectorAll('.type-chip').forEach(c => c.addEventListener('click', () => {
      document.querySelectorAll('.type-chip').forEach(x => x.classList.remove('active'));
      c.classList.add('active');
      manner = c.dataset.type;
      document.getElementById('price-group').style.display = manner === 'limit' ? '' : 'none';
      document.getElementById('expiry-group').style.display = manner === 'limit' ? '' : 'none';
      label(); estimate();
    }));

    document.querySelectorAll('.pct-chip').forEach(c => c.addEventListener('click', () => {
      const amt = document.getElementById('order-amount');
      amt.value = ((2500 * parseFloat(c.dataset.pct))).toFixed(2);
      estimate();
    }));

    ['order-amount', 'order-price'].forEach(id => {
      const e = document.getElementById(id);
      if (e) e.addEventListener('input', estimate);
    });

    document.getElementById('order-preview-btn').addEventListener('click', review);
    document.getElementById('order-modal-close').addEventListener('click', closeModal);
    document.getElementById('order-exec-btn').addEventListener('click', commitBench);
    document.getElementById('order-copy-btn').addEventListener('click', () => {
      navigator.clipboard.writeText(document.getElementById('order-cli-cmd').textContent)
        .then(() => Bench.toast('Command copied'), () => Bench.toast('Clipboard unavailable'));
    });

    label();
    estimate();
  }

  function label() {
    const btn = document.getElementById('order-preview-btn');
    btn.textContent = `${side === 0 ? 'Acquire' : 'Release'} · ${manner === 'limit' ? 'Standing Limit' : 'Immediate'}`;
    btn.className = `btn btn-block ${side === 0 ? 'btn-buy' : 'btn-sell'}`;
  }

  function estimate() {
    const sub = document.getElementById('order-estimate-sub');
    const amt = parseFloat(document.getElementById('order-amount').value) || 0;
    if (amt <= 0) { sub.textContent = 'Continuous depth against the salon pool'; return; }

    let p = Demo.state.spot / Bench.COIN;
    if (manner === 'limit') {
      const c = parseFloat(document.getElementById('order-price').value);
      if (c > 0) p = c;
    }
    const gross = amt * p;
    const fee = gross * 0.01;
    const net = side === 0 ? gross + fee : gross - fee;

    sub.innerHTML = side === 0
      ? `Consideration <strong style="color:var(--ink-100)">${net.toFixed(4)} HΞ∆Ŧ</strong> · salon fee 1% · 70% to CD yield`
      : `Proceeds <strong style="color:var(--ink-100)">${net.toFixed(4)} HΞ∆Ŧ</strong> · net of salon fee 1% · 70% to CD yield`;
  }

  function review() {
    const amt = parseFloat(document.getElementById('order-amount').value) || 0;
    if (amt <= 0) { Bench.toast('Enter an amount in XFG'); return; }

    let p = (Demo.state.spot / Bench.COIN).toFixed(4);
    let term = 'At the pool mid';
    if (manner === 'limit') {
      const v = parseFloat(document.getElementById('order-price').value);
      if (!(v > 0)) { Bench.toast('Enter a limit price'); return; }
      p = v.toFixed(4);
      const b = document.getElementById('order-expiry').value || '4320';
      term = `${b} blocks · ~${(parseInt(b) / 720).toFixed(1)} epochs`;
    }

    const gross = (amt * parseFloat(p)).toFixed(4);
    const cd = (amt * parseFloat(p) * 0.01 * 0.70).toFixed(4);
    const field = (l, v, tone) =>
      `<div><span class="rf-label">${l}</span><span class="rf-value"${tone ? ` style="color:var(${tone})"` : ''}>${v}</span></div>`;

    document.getElementById('order-modal-details').innerHTML = `
      <div class="rf">
        <div class="rf-head">
          <span class="rf-ref">Bench · ${(Math.random() * 0xffff).toString(16).toUpperCase().padStart(4, '0')}</span>
          <span class="badge ${side === 0 ? 'badge-green' : 'badge-red'}">${side === 0 ? 'Acquire XFG' : 'Release XFG'}</span>
        </div>
        <div class="rf-grid">
          ${field('Manner', manner === 'limit' ? 'Standing Limit' : 'Immediate (Pool)')}
          ${field('Amount', amt.toFixed(2) + ' XFG', '--maison-bright')}
          ${field('Price', p + ' HΞ∆Ŧ', '--heat-bright')}
          ${field('Consideration', '≈ ' + gross + ' HΞ∆Ŧ')}
          ${field('Salon Fee 1%', '70% CD yield · ' + cd + ' HΞ∆Ŧ')}
          ${field('Term', term)}
        </div>
      </div>
      <p class="rf-note">
        The bench simulates the standing-order sheet. Nothing is broadcast, signed,
        or committed; no vault is consulted.
      </p>`;

    const cli = document.getElementById('order-cli-cmd');
    cli.textContent = manner === 'limit'
      ? `fire_wallet place_order ${side === 0 ? 'buy' : 'sell'} ${amt} ${p} ${document.getElementById('order-expiry').value || 4320}`
      : `fire_wallet amm_swap ${side === 0 ? 0 : 1} ${Math.round(amt * Bench.COIN)}`;
    cli.style.display = 'block';

    document.getElementById('order-modal').classList.add('active');
  }

  function closeModal() { document.getElementById('order-modal').classList.remove('active'); }

  function commitBench() {
    closeModal();
    Bench.toast('Bench: no order was broadcast — demonstration only');
  }

  // ── The bench walks forward on its own ─────────────────────────────────────
  function tick() {
    Demo.step();
    // Extend the live bar so the chart advances like a real one.
    if (series.length) {
      const last = series[series.length - 1];
      last.c = Demo.state.spot;
      last.h = Math.max(last.h, Demo.state.spot);
      last.l = Math.min(last.l, Demo.state.spot);
      last.v += 12 * Bench.COIN * Demo.rand();
    }
    applySeries();
    renderBook();
    renderRegister();
  }

  function init() {
    initThemeSwitcher();
    onTheme(() => {
      if (chart) {
        try { chart.setStyles(ChartStyle.build(tokens(CHART_TOKENS))); }
        catch (e) { console.warn('[bench] theme repaint failed:', e); }
      }
      Archive.repaint();
    });

    // A bench is presented live, so the connection lamps read connected. The
    // header states plainly that the data is synthesised.
    ['daemon', 'wallet', 'swapd'].forEach(k => {
      const d = document.getElementById('status-' + k);
      if (d) { d.className = 'status-dot online'; d.title = 'Bench — synthesised'; }
    });

    loadSeries();
    initChart();
    applySeries();
    Archive.init();
    initTicket();
    renderBook();
    renderRegister();

    document.querySelectorAll('.ohlcv-tf').forEach(b => b.addEventListener('click', () => {
      document.querySelectorAll('.ohlcv-tf').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
      tf = b.dataset.tf;
      loadSeries();
    }));

    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') closeModal();
    });

    setInterval(tick, 4000);
  }

  return { init };
})();

document.addEventListener('DOMContentLoaded', HearthTest.init);
