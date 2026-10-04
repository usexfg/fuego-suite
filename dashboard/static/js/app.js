// ── Fuego Dashboard — Shared App Layer ─────────────────────────────────────────
'use strict';

const App = (() => {
  let ws = null;
  let wsReconnectTimer = null;
  const listeners = {};
  let health = { daemon: false, wallet: false, swapd: false };

  // ── Theme ──────────────────────────────────────────────────────────────────
  // Three registered registers. The choice is cosmetic only: it is persisted,
  // restored before first paint, and announced so charts can re-read their
  // tokens. Directional colour never changes meaning between registers.
  const THEMES = ['maison', 'fulltrade', 'reference'];
  const THEME_KEY = 'xfg.theme';
  let theme = 'reference';

  function storedTheme() {
    try {
      const v = localStorage.getItem(THEME_KEY);
      return THEMES.includes(v) ? v : null;
    } catch (e) { return null; }
  }

  function applyTheme(name, persist) {
    if (!THEMES.includes(name)) return;
    theme = name;
    document.documentElement.setAttribute('data-theme', name);
    if (persist !== false) { try { localStorage.setItem(THEME_KEY, name); } catch (e) { /* private mode */ } }
    syncThemeSwitcher();
    emit('theme', name);
  }

  function syncThemeSwitcher() {
    document.querySelectorAll('.theme-opt').forEach(b => {
      b.setAttribute('aria-pressed', String(b.dataset.theme === theme));
    });
  }

  function initThemeSwitcher() {
    // Restored here rather than at module load so the very first paint is
    // already in the right register — no flash of the default.
    applyTheme(storedTheme() || theme, false);

    document.querySelectorAll('.theme-opt').forEach(btn => {
      btn.addEventListener('click', () => applyTheme(btn.dataset.theme, true));
    });
    syncThemeSwitcher();
  }

  // Read the live token values. Charts must resolve colours through this rather
  // than caching them, so they follow a theme change.
  function tokens(names) {
    const cs = getComputedStyle(document.documentElement);
    const out = {};
    names.forEach(n => { out[n] = cs.getPropertyValue(n).trim(); });
    return out;
  }

  // ── WebSocket ──

  function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${proto}//${location.host}/ws/blocks`;
    ws = new WebSocket(url);

    ws.onopen = () => {
      console.log('[ws] connected');
      updateStatusDot('ws', true);
    };

    ws.onmessage = (evt) => {
      try {
        const event = JSON.parse(evt.data);
        emit(event.type, event.payload);
      } catch (e) {
        console.warn('[ws] parse error:', e);
      }
    };

    ws.onclose = () => {
      console.log('[ws] disconnected, reconnecting in 3s');
      updateStatusDot('ws', false);
      wsReconnectTimer = setTimeout(connectWS, 3000);
    };

    ws.onerror = () => ws.close();
  }

  function updateStatusDot(id, online) {
    const dot = document.getElementById(`status-${id}`);
    if (dot) {
      dot.className = `status-dot ${online ? 'online' : 'offline'}`;
    }
  }

  // ── Event Emitter ──

  function on(type, fn) {
    if (!listeners[type]) listeners[type] = [];
    listeners[type].push(fn);
  }

  function emit(type, payload) {
    (listeners[type] || []).forEach(fn => {
      try { fn(payload); } catch (e) { console.error(`[event ${type}]`, e); }
    });
  }

  // ── API Client ──

  async function daemonGet(path) {
    const resp = await fetch(path);
    return resp.json();
  }

  // Non-JSON-RPC daemon routes use a flat JSON request/response body.
  async function daemonPost(path, params = {}) {
    const resp = await fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(params)
    });
    if (!resp.ok) throw new Error(`daemon HTTP ${resp.status}`);
    const data = await resp.json();
    if (data.status && data.status !== 'OK') throw new Error(data.status);
    return data;
  }

  // Wallet RPC proxy — browser never touches the access key
  async function walletRpc(method, params = {}) {
    const resp = await fetch('/api/wallet', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Fuego-Operator': '1' },
      body: JSON.stringify({ jsonrpc: '2.0', id: 'dash', method, params })
    });
    const data = await resp.json();
    if (data.error) throw new Error(data.error.message || JSON.stringify(data.error));
    return data.result;
  }

  // Cross-chain execution belongs to xfg-swapd (18902), not walletd. The Go
  // dashboard proxy injects the optional control token so it never reaches the
  // browser or copied commands.
  async function swapRpc(method, params = {}) {
    const resp = await fetch('/api/swapd-rpc', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Fuego-Operator': '1' },
      body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params })
    });
    const text = await resp.text();
    let data;
    try {
      data = JSON.parse(text);
    } catch {
      throw new Error(text.trim() || `xfg-swapd HTTP ${resp.status}`);
    }
    if (!resp.ok) throw new Error(data.error?.message || `xfg-swapd HTTP ${resp.status}`);
    if (data.error) throw new Error(data.error.message || JSON.stringify(data.error));
    return data.result;
  }

  async function getHealth() {
    try {
      const resp = await fetch('/api/health');
      health = await resp.json();
      updateStatusDot('daemon', health.daemon);
      updateStatusDot('wallet', health.wallet);
      updateStatusDot('swapd', health.swapd);
    } catch {
      health = { daemon: false, wallet: false, swapd: false };
      updateStatusDot('daemon', false);
      updateStatusDot('wallet', false);
      updateStatusDot('swapd', false);
    }
  }

  // ── Formatting ──

  const COIN = 10000000; // 1e7

  function fmtXfg(atomic) {
    if (atomic == null) return '—';
    return (Number(atomic) / COIN).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 4 });
  }

  function fmtHeat(atomic) {
    if (atomic == null) return '—';
    return (Number(atomic) / COIN).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 4 });
  }

  function fmtPct(ratio) {
    if (ratio == null) return '—';
    return (Number(ratio) * 100).toFixed(2) + '%';
  }

  function fmtPrice(price) {
    if (price == null) return '—';
    const v = Number(price) / COIN;
    if (v >= 1000) return v.toLocaleString(undefined, { maximumFractionDigits: 0 });
    if (v >= 1) return v.toFixed(2);
    return v.toFixed(6);
  }

  function fmtTime(ts) {
    if (!ts) return '—';
    const d = new Date(typeof ts === 'number' ? ts * 1000 : ts);
    return d.toLocaleTimeString();
  }

  function fmtHeight(h) {
    return h != null ? `#${Number(h).toLocaleString()}` : '—';
  }

  function fmtDuration(seconds) {
    if (!seconds || seconds < 0) return '—';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m`;
    return `${Math.floor(seconds)}s`;
  }

  // ── Clipboard ──

  function copyToClipboard(text) {
    navigator.clipboard.writeText(text).then(() => {
      showToast('Copied to clipboard');
    });
  }

  function showToast(msg) {
    let toast = document.getElementById('app-toast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'app-toast';
      toast.style.cssText = 'position:fixed;bottom:24px;right:24px;background:var(--bg-tertiary);color:var(--text-primary);border:1px solid var(--border);padding:10px 16px;border-radius:6px;font-size:13px;z-index:9999;transition:opacity 0.3s;opacity:0;';
      document.body.appendChild(toast);
    }
    toast.textContent = msg;
    toast.style.opacity = '1';
    setTimeout(() => { toast.style.opacity = '0'; }, 2000);
  }

  // ── Health Polling ──

  function startHealthPolling() {
    getHealth();
    setInterval(getHealth, 15000);
  }

  // ── Init ──

  function init() {
    initThemeSwitcher();
    connectWS();
    startHealthPolling();
  }
  return {
    init, on, daemonGet, daemonPost, walletRpc, swapRpc,
    fmtXfg, fmtHeat, fmtPct, fmtPrice, fmtTime, fmtHeight, fmtDuration,
    copyToClipboard, showToast,
    tokens, applyTheme, initThemeSwitcher,
    get theme() { return theme; },
    get THEMES() { return THEMES.slice(); },
    get health() { return health; },
    get COIN() { return COIN; }
  };
})();

document.addEventListener('DOMContentLoaded', App.init);
