// ── XFG/USD price archive ─────────────────────────────────────────────────────
// Real daily XFG/USD history, shipped as a static asset and read by whichever
// page needs it. Kept on its own axis and its own panel: the Hearth pool quotes
// HΞΔŦ per XFG at the $1.58 reference, which is a different unit and a
// different magnitude, so the two series are never stacked or shared.
//
// Pure data: no DOM, no chart library, no page state. The live page and the
// bench both mount it, which is why it lives here rather than inside either.
'use strict';

const XfgArchive = (() => {
  const URL = '/data/xfg_historical_prices.json';
  const PEG_USD = 1.58;   // the protocol's HΞΔŦ reference
  const MIN_BARS = 2;

  let raw = [];
  let meta = null;
  let inflight = null;

  // Prices span three orders of magnitude ($0.0001 to $0.07), so a fixed
  // decimal count would truncate the early history. Scale precision to value.
  const usd = v => {
    const a = Math.abs(v);
    if (a >= 0.1) return v.toFixed(4);
    if (a >= 0.001) return v.toFixed(5);
    return v.toFixed(6);
  };
  const vol = v => {
    if (!v) return '0';
    if (v >= 1e6) return (v / 1e6).toFixed(2) + 'M';
    if (v >= 1e3) return (v / 1e3).toFixed(2) + 'k';
    return String(Math.round(v));
  };
  const day = ts => new Date(ts * 1000).toLocaleDateString('en-US',
    { year: 'numeric', month: 'short', day: 'numeric' });

  // A malformed bar must be dropped, not coerced: carrying it into a weekly
  // roll-up would quietly change that week's high or low.
  const isCandle = c => c && Number.isFinite(c.period_start)
    && Number.isFinite(c.open) && Number.isFinite(c.high)
    && Number.isFinite(c.low) && Number.isFinite(c.close)
    && c.high >= Math.max(c.open, c.close) && c.low <= Math.min(c.open, c.close);

  // Weekly buckets keyed by the Monday that starts the ISO week. Opens at the
  // first daily open, closes at the last daily close, volume summed.
  function weekly(rows) {
    const out = [];
    let cur = null, key = null;
    for (const r of rows) {
      const d = new Date(r.period_start * 1000);
      d.setUTCHours(0, 0, 0, 0);
      const monday = new Date(d.getTime() - ((d.getUTCDay() + 6) % 7) * 86400000);
      const k = monday.getTime();
      if (k !== key) {
        key = k;
        cur = { timestamp: k, open: r.open, high: r.high, low: r.low, close: r.close, volume: r.volume };
        out.push(cur);
      } else {
        cur.high = Math.max(cur.high, r.high);
        cur.low = Math.min(cur.low, r.low);
        cur.close = r.close;
        cur.volume += r.volume;
      }
    }
    return out;
  }

  const daily = () => raw.map(r => ({
    timestamp: r.period_start * 1000,
    open: r.open, high: r.high, low: r.low, close: r.close, volume: r.volume,
  }));

  // The source is daily, so only 1D and 1W exist. 1H/4H are not resampled from
  // nothing: callers get the nearest honest series instead.
  function series(tf) {
    return tf === '1w' ? weekly(raw) : daily();
  }

  function span() {
    if (!raw.length) return null;
    return { first: day(raw[0].period_start), last: day(raw[raw.length - 1].period_start) };
  }

  // A failed load is not cached: a retry must be able to succeed.
  function load() {
    if (raw.length) return Promise.resolve(raw);
    if (inflight) return inflight;
    inflight = fetch(URL, { cache: 'no-cache' })
      .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .then(payload => {
        if (!payload || !Array.isArray(payload.candles)) throw new Error('malformed archive');
        const rows = payload.candles.filter(isCandle)
          .slice().sort((a, b) => a.period_start - b.period_start);
        if (rows.length < MIN_BARS) throw new Error('archive too short');
        raw = rows;
        meta = payload.meta || null;
        return raw;
      })
      .catch(err => { inflight = null; throw err; });
    return inflight;
  }

  return {
    load, series, daily, weekly, span, usd, vol, day, PEG_USD,
    get rows() { return raw.length; },
    get meta() { return meta; },
  };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = XfgArchive;
