// ── Fuego Dashboard — Shared App Layer ─────────────────────────────────────────
'use strict';

const App = (() => {
  let ws = null;
  let wsReconnectTimer = null;
  const listeners = {};
  let health = { daemon: false, wallet: false, swapd: false };

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

  async function rpc(method, params = {}) {
    const resp = await fetch('/json_rpc', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ jsonrpc: '2.0', id: 'dash', method, params })
    });
    const data = await resp.json();
    if (data.error) throw new Error(data.error.message || JSON.stringify(data.error));
    return data.result;
  }

  async function daemonGet(path) {
    const resp = await fetch(path);
    return resp.json();
  }

  // Wallet RPC proxy — browser never touches the access key
  async function walletRpc(method, params = {}) {
    const resp = await fetch('/api/wallet', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ jsonrpc: '2.0', id: 'dash', method, params })
    });
    const data = await resp.json();
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
    connectWS();
    startHealthPolling();
  }

  return {
    init, on, rpc, daemonGet, walletRpc,
    fmtXfg, fmtHeat, fmtPct, fmtPrice, fmtTime, fmtHeight, fmtDuration,
    copyToClipboard, showToast,
    get health() { return health; },
    get COIN() { return COIN; }
  };
})();

document.addEventListener('DOMContentLoaded', App.init);
