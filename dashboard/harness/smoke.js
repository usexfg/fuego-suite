// Smoke harness: runs each dashboard page's script against a minimal DOM so
// that a missing element or a typo throws here instead of silently in a
// browser. Chart libraries are stubbed; only the wiring is under test.
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', 'static');

class El {
  constructor(id, cls, onChange) {
    this._id = id || '';
    this.className = cls || '';
    this._onChange = onChange || null;
    this.dataset = {};
    this.style = new Proxy({ cssText: '' }, { get: (t, k) => t[k] ?? '', set: (t, k, v) => (t[k] = v, true) });
    this.children = [];
    this._html = '';
    this.textContent = '';
    this.hidden = false;
    this.listeners = {};
    const set = new Set();
    this.classList = {
      add: (...c) => c.forEach(x => set.add(x)),
      remove: (...c) => c.forEach(x => set.delete(x)),
      toggle: (c, force) => {
        const on = force === undefined ? !set.has(c) : !!force;
        if (on) set.add(c); else set.delete(c);
        return on;
      },
      contains: c => set.has(c),
    };
  }
  addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
  removeEventListener() {}
  // An element that acquires an id or a class after creation must become
  // findable, exactly as it would in a real DOM — otherwise anything the page
  // builds at runtime (toasts, offline notices) is invisible to the assertions.
  get id() { return this._id; }
  set id(v) { this._id = v || ''; if (this._onChange) this._onChange(this); }
  set className(v) { this._cls = v; if (this._onChange) this._onChange(this); }
  get className() { return this._cls || ''; }
  dispatchEvent(t) { (this.dispatch(t) || []); return true; }
  dispatch(t) { (this.listeners[t] || []).forEach(f => f({ preventDefault() {}, key: '', target: this })); }
  set innerHTML(v) { this._html = v; this.children = []; }
  get innerHTML() { return this._html; }
  appendChild(c) { this.children.push(c); return c; }
  querySelectorAll() { return []; }
  querySelector() { return null; }
  closest() { return null; }
  getBoundingClientRect() { return { width: 800, height: 400, top: 0, left: 0 }; }
  setAttribute() {} removeAttribute() {}
  get firstChild() { return this.children[0] || null; }
  focus() {} click() {} remove() {}
}

// Every id and class the page declares, so the shim answers lookups truthfully.
function buildDom(html) {
  const ids = [...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]);
  const classes = new Set();
  for (const m of html.matchAll(/class="([^"]*)"/g)) m[1].split(/\s+/).forEach(c => c && classes.add(c));

  const registry = {};
  ids.forEach(id => { registry[id] = new El(id); });

  // Real element handles, carrying their own data-* attributes, so a control's
  // click handler can be driven for real. Without this, a selector like
  // `.archive-tf` yields blank stubs whose `dataset` is empty and every branch
  // downstream of it goes untested.
  const byClass = new Map();
  for (const m of html.matchAll(/<([a-z][a-z0-9]*)\b([^>]*)>/gi)) {
    const attrs = m[2];
    const cm = attrs.match(/class="([^"]*)"/);
    if (!cm) continue;
    const el = new El();
    const idm = attrs.match(/id="([^"]*)"/);
    if (idm) el.id = idm[1];
    for (const a of attrs.matchAll(/data-([a-z-]+)="([^"]*)"/g)) {
      el.dataset[a[1].replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = a[2];
    }
    for (const c of cm[1].split(/\s+/)) {
      if (!c) continue;
      if (!byClass.has(c)) byClass.set(c, []);
      byClass.get(c).push(el);
    }
  }
  // Runtime-generated hooks the script legitimately creates.
  ['ladder-row', 'ob-row', 'rf', 'rf-head', 'rf-ref', 'rf-grid', 'rf-label', 'rf-value',
   'rf-note', 'progress-step', 'chain-rates', 'ordergraph-col', 'ordergraph-depth',
   'ordergraph-marker', 'bridge-chain-row', 'step', 'done'].forEach(c => classes.add(c));

  const fakeClassList = () => ({ add() {}, remove() {}, toggle() {}, contains: () => false });

  const doc = new El('#document');
  // Elements the page creates at runtime register here, so getElementById and
  // the class selectors can find them just as a real DOM would.
  const liveIds = new Map();
  const liveClasses = new Map();
  const track = el => {
    if (el.id) liveIds.set(el.id, el);
    for (const c of String(el.className || '').split(/\s+/)) {
      if (!c) continue;
      if (!liveClasses.has(c)) liveClasses.set(c, []);
      liveClasses.get(c).push(el);
    }
  };
  doc.documentElement = new El('html');
  doc.documentElement.style = {};
  doc.documentElement.setAttribute = (k, v) => { doc.documentElement[k] = v; };
  doc.head = new El('head');
  doc.body = new El('body');
  const byId = id => registry[id] || liveIds.get(id) || null;
  doc.getElementById = byId;
  doc.createElement = t => new El('', 'created-' + t, track);
  doc.createElementNS = (ns, t) => new El('', 'ns-' + t, track);
  doc.createTextNode = t => new El('', 'text');
  doc.createDocumentFragment = () => new El('', 'frag');
  doc.querySelectorAll = sel => {
    const c = sel.replace(/^[.#]/, '');
    if (byClass.has(c)) return byClass.get(c);
    if (liveClasses.has(c)) return liveClasses.get(c);
    return classes.has(c) ? Array.from({ length: 4 }, () => new El('', c)) : [];
  };
  doc.querySelector = sel => {
    const c = sel.replace(/^[.#]/, '');
    if (byClass.has(c)) return byClass.get(c)[0] || null;
    if (liveClasses.has(c)) return liveClasses.get(c)[0] || null;
    return classes.has(c) ? new El('', c) : null;
  };
  global.document = doc;
  Object.defineProperty(global.document, 'getElementById', { value: byId });

  // Stylesheet tokens. A real stylesheet is not parsed here, so each token
  // resolves to a distinct plausible value per theme: that is enough to catch
  // a chart that reads a token the stylesheet does not actually define, and to
  // prove a theme change reaches the chart.
  const SHEETS = {
    maison:    { '--dir-up':'#c4453d','--dir-up-line':'rgba(196,69,61,.32)','--dir-down':'#5b8aa8','--dir-down-line':'rgba(91,138,168,.32)','--ink-30':'#6a6a6a','--ink-20':'#4a4a4a','--ink-50':'#8a8a8a','--ink-90':'#d0d0d0','--hairline':'rgba(255,255,255,.09)','--hairline-soft':'rgba(255,255,255,.05)','--accent':'#bfa470','--accent-line':'rgba(191,164,112,.3)','--flame':'#8fa6b4','--surface-active':'#202126','--surface-panel':'#0e0e10','--font-mono':'IBM Plex Mono' },
    fulltrade: { '--dir-up':'#ff3b2f','--dir-up-line':'rgba(255,59,47,.38)','--dir-down':'#2e8bc0','--dir-down-line':'rgba(46,139,192,.38)','--ink-30':'#6b6b6b','--ink-20':'#4a4a52','--ink-50':'#8a8a96','--ink-90':'#f2f2f5','--hairline':'rgba(255,255,255,.12)','--hairline-soft':'rgba(255,255,255,.06)','--accent':'#ff6b35','--accent-line':'rgba(255,107,53,.4)','--flame':'#9fd8ff','--surface-active':'#2e3040','--surface-panel':'#1a1b23','--font-mono':'IBM Plex Mono' },
    reference: { '--dir-up':'#f7768e','--dir-up-line':'rgba(247,118,142,.34)','--dir-down':'#7aa2f7','--dir-down-line':'rgba(122,162,247,.34)','--ink-30':'#6a77ab','--ink-20':'rgba(152,160,200,.14)','--ink-50':'#7281c3','--ink-90':'#9daceb','--hairline':'rgba(152,160,200,.11)','--hairline-soft':'rgba(152,160,200,.06)','--accent':'#e0af68','--accent-line':'rgba(224,175,104,.32)','--flame':'#7dcfff','--surface-active':'#20202c','--surface-panel':'#0e0e15','--font-mono':'IBM Plex Mono' }
  };
  // Resolved from the attribute the page itself sets, so the harness cannot
  // pass by flipping a variable the real code never reads.
  const activeSheet = () => SHEETS[doc.documentElement['data-theme']] || SHEETS.reference;
  global.getComputedStyle = () => ({ getPropertyValue: t => activeSheet()[t] || '#888888' });
  const setTheme = name => { doc.documentElement.setAttribute('data-theme', name); };
  const store = {};
  global.localStorage = {
    getItem: k => (k in store ? store[k] : null),
    setItem: (k, v) => { store[k] = String(v); },
    removeItem: k => { delete store[k]; }
  };

  const nav = { clipboard: { writeText: () => Promise.resolve() }, platform: 'MacIntel', userAgent: 'harness', language: 'en-US' };
  global.navigator = nav;
  global.window = {
    addEventListener() {},
    location: { protocol: 'http:', host: '127.0.0.1:18918', platform: 'MacIntel', href: 'http://127.0.0.1:18918/' },
    getComputedStyle: global.getComputedStyle,
    navigator: nav,
    document: global.document
  };
  global.location = global.window.location;
  global.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
  // The chart library sniffs the user agent; a non-browser string is fine.
  // The archive asset is served for real so its fetch/validate/roll-up path is
  // exercised rather than skipped behind the offline stub.
  const ARCHIVE = path.join(ROOT, 'data/xfg_historical_prices.json');
  const archiveBytes = fs.statSync(ARCHIVE).size;
  const archiveDoc = JSON.parse(fs.readFileSync(ARCHIVE, 'utf8'));
  const fails = { archive: false };
  global.fetch = url => {
    if (String(url).includes('xfg_historical_prices.json')) {
      if (fails.archive) return Promise.reject(new Error('archive offline in harness'));
      return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(archiveDoc) });
    }
    return Promise.reject(new Error('offline in harness'));
  };
  global.WebSocket = class {
    constructor() { this.readyState = 0; }
    close() {}
  };
  global.setTimeout = setTimeout; global.setInterval = () => 0; global.clearTimeout = clearTimeout;

  // Chart libraries: record calls, assert nothing throws.
  // The overlay API mirrors klinecharts 9.8.12 exactly — it has removeOverlay(id)
  // and getCompleteOverlays(), and NO removeAllOverlay. A stub that invents the
  // missing method hides a real TypeError from the page under test.
  const calls = [];
  let overlays = [];
  const chartStub = () => new Proxy({
    subscribeAction() {},
    setStyles(st) { calls.push(['setStyles', st.candle.bar.upColor + '|' + st.candle.bar.downColor]); },
    applyNewData(d) { calls.push(['applyNewData', d.length]); },
    resize() {}, removeIndicator() {},
    createIndicator() { calls.push(['createIndicator', '']); },
    createOverlay(c) { overlays.push({ id: (c && c.id) || 'ov' + overlays.length }); calls.push(['createOverlay', '']); },
    getCompleteOverlays() { return overlays.slice(); },
    removeOverlay(id) {
      overlays = overlays.filter(o => o.id !== id);
      calls.push(['removeOverlay', id]);
    },
    addLineSeries() {
      return { applyOptions: o => { if (o && o.color) calls.push(['applyOptions', o.color]); } };
    },
    addAreaSeries() { return { applyOptions() {} }; },
    applyOptions() {},
    setData() {}
  }, { get: (t, k) => (k in t ? t[k] : () => {}) });
  global.klinecharts = { init: () => chartStub() };
  global.LightweightCharts = { createChart: () => chartStub() };

  return {
    registry, liveIds, liveClasses, classes, calls, byClass, fails, setTheme,
    get theme() { return doc.documentElement['data-theme']; },
    archive: { bytes: archiveBytes, rows: archiveDoc.candles.length },
  };
}

const PAGES = [
  ['hearth.html', ['vendor/klinecharts.min.js', 'js/archive.js', 'js/app.js', 'js/hearth.js'], 'Hearth'],
  ['hearthtest.html', ['vendor/klinecharts.min.js', 'js/archive.js', 'js/hearthtest.js'], 'HearthTest'],
  ['swapxfg.html', ['vendor/lightweight-charts.standalone.production.js', 'js/app.js', 'js/swapxfg.js'], 'SwapXFG']
];

const THEMES = ['maison', 'fulltrade', 'reference'];

let failures = 0;
(async () => {
for (const [page, scripts, globalName] of PAGES) {
  for (const startTheme of THEMES) {
    const html = fs.readFileSync(path.join(ROOT, page), 'utf8');
    const dom = buildDom(html);
    process.stdout.write(`\n── ${page}  [${startTheme}]\n`);
    try {
      // All scripts for a page go into ONE eval: they are separate files in
      // the browser (shared global scope) but separate evals here, which would
      // hide app.js's `App` from the page module.
      const bundle = scripts
        .filter(f => !f.startsWith('vendor/'))   // chart libs are stubbed above
        .map(f => fs.readFileSync(path.join(ROOT, f), 'utf8'))
        .join('\n;\n') + `
;globalThis.__mod     = (typeof ${globalName} !== 'undefined') ? ${globalName} : undefined;
;globalThis.__archive = (typeof Archive !== 'undefined') ? Archive : undefined;
;globalThis.__data    = (typeof XfgArchive !== 'undefined') ? XfgArchive : undefined;
;globalThis.__apply   = (typeof App !== 'undefined' && App.applyTheme) ? App.applyTheme
                      : (typeof applyTheme !== 'undefined') ? applyTheme : undefined;`;
      (0, eval)(bundle);
      const mod = global.__mod;
      const archive = global.__archive;
      const data = global.__data;
      const apply = global.__apply;
      global.__mod = global.__apply = global.__archive = global.__data = undefined;
      if (typeof mod !== 'object' || typeof mod.init !== 'function') {
        throw new Error(`${globalName} did not export an init()`);
      }
      if (typeof apply !== 'function') {
        throw new Error('no applyTheme() reachable — the register switch would be inert');
      }
      dom.setTheme(startTheme);
      mod.init();

      // Cycle every register and require the chart to be repainted in each,
      // with the new palette actually reaching the plot.
      const repaints = [];
      for (const t of THEMES) {
        const before = dom.calls.length;
        apply(t, false);
        const added = dom.calls.slice(before);
        const style = added.find(c => c[0] === 'setStyles') || added.find(c => c[0] === 'applyOptions');
        if (!style) throw new Error(`register "${t}" did not repaint the chart`);
        repaints.push(style[1].split('|')[0]);
      }
      const distinct = new Set(repaints).size;
      if (distinct < 2) throw new Error('register change did not alter chart colours');

      let extra = '';
      // Both pages read the archive, but they mount it differently: the bench
      // has a dedicated panel, the live page plots it on the main chart. The
      // data module is shared, so it is what drives the load to completion
      // here; the per-page assertions follow.
      if (data) {
        await data.load();
        for (let i = 0; i < 3; i++) await new Promise(r => setImmediate(r));
        const bars = dom.calls.filter(c => c[0] === 'applyNewData').map(c => c[1]);
        if (!bars.includes(dom.archive.rows)) {
          throw new Error(`archive did not plot ${dom.archive.rows} daily bars (saw ${bars.join(',')})`);
        }
      }

      // The bench panel: subtitle carries the true span so a stale archive
      // cannot read as live, and 1W discloses that it is a roll-up.
      if (globalName === 'HearthTest') {
        const sub = dom.registry['archive-sub'];
        if (!sub || !/2,\d{3} daily closes/.test(sub.textContent)) {
          throw new Error(`archive subtitle not populated: ${sub && sub.textContent}`);
        }
        const st = dom.registry['archive-state'];
        if (st.hidden !== true) throw new Error(`archive overlay still showing: "${st.textContent}"`);

        const weekBtn = (dom.byClass.get('archive-tf') || []).find(b => b.dataset.atf === '1w');
        if (!weekBtn) throw new Error('no 1W archive control found');
        weekBtn.dispatch('click');
        const wk = dom.calls.filter(c => c[0] === 'applyNewData').map(c => c[1]).at(-1);
        if (!(wk > 0 && wk < dom.archive.rows / 6)) {
          throw new Error(`1W roll-up produced ${wk} bars, expected ~${Math.round(dom.archive.rows / 7)}`);
        }
        if (!/Weekly roll-up/.test(st.textContent)) {
          throw new Error('1W did not disclose that it is a roll-up');
        }
        extra = ` · archive ${dom.archive.rows}d → ${wk}w`;
      }

      // The live page must never fabricate candles. With no RPC available the
      // main chart must carry the real archive — not mock prices, and not a
      // blank panel with a note — and must declare which unit it now shows.
      if (globalName === 'Hearth') {
        const bars = dom.calls.filter(c => c[0] === 'applyNewData').map(c => c[1]);
        if (bars.includes(0)) throw new Error('main chart plotted an empty series');
        if (!bars.includes(dom.archive.rows)) {
          throw new Error(`main chart did not plot the ${dom.archive.rows}-bar archive (saw ${bars.join(',')})`);
        }
        const cap = dom.registry['chart-caption'];
        if (!cap || !/XFG \/ USD/.test(cap.textContent)) {
          throw new Error(`chart caption does not declare the unit: ${cap && cap.textContent}`);
        }
        if (!/not the pool price/.test(cap.textContent)) {
          throw new Error('caption does not warn that this is not the pool price');
        }
        // 1W on the main chart must re-plot the weekly roll-up.
        const weekBtn = (dom.byClass.get('ohlcv-tf') || []).find(b => b.dataset.tf === '1w');
        if (!weekBtn) throw new Error('no 1W control on the main chart');
        weekBtn.dispatch('click');
        for (let i = 0; i < 4; i++) await new Promise(r => setImmediate(r));
        const wk = dom.calls.filter(c => c[0] === 'applyNewData').map(c => c[1]).at(-1);
        if (!(wk > 0 && wk < dom.archive.rows / 6)) {
          throw new Error(`main chart 1W produced ${wk} bars`);
        }
        if (!/weekly/.test(dom.registry['chart-caption'].textContent)) {
          throw new Error('caption did not follow the timeframe change');
        }
        extra += ' · main chart shows real archive';
      }

      // The overlay controls must work against the real 9.8 API. Drawing then
      // clearing has to remove what it added.
      const draw = dom.byClass.get('draw-btn') || [];
      const seg = draw.find(b => b.dataset.tool === 'segment');
      const clr = draw.find(b => b.dataset.tool === 'clear');
      if (seg && clr) {
        seg.dispatch('click');
        clr.dispatch('click');
        if (!dom.calls.some(c => c[0] === 'removeOverlay')) {
          throw new Error('clear-overlays removed nothing');
        }
      }

      console.log(`   init ok · repainted ${repaints.length} registers, ${distinct} distinct palettes${extra}`);
    } catch (e) {
      failures++;
      console.log(`   FAILED: ${e.message}`);
      console.log(e.stack.split('\n').slice(1, 4).join('\n'));
    }
  }
}

console.log(failures ? `\n${failures} case(s) failed\n` : '\nall pages initialise cleanly in every register\n');
process.exit(failures ? 1 : 0);
})();
