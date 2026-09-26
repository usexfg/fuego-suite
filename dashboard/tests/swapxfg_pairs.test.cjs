const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const repo = path.resolve(__dirname, '../..');
const catalogSource = fs.readFileSync(path.join(repo, 'src/SwapDaemon/SwapPairCatalog.h'), 'utf8');
const dashboardSource = fs.readFileSync(path.join(repo, 'dashboard/static/js/swapxfg.js'), 'utf8');
const catalog = [...catalogSource.matchAll(/^\s*X\(([A-Z][A-Z0-9_]*),\s*(\d+),/gm)]
  .map(([, symbol, id]) => ({ symbol, id: Number(id) }));

function createDashboard() {
  const elements = new Map();
  const select = {
    value: '', options: [],
    set innerHTML(_) { this.options = []; this.value = ''; },
    appendChild(option) { this.options.push(option); },
    prepend(option) { this.options.unshift(option); }
  };
  elements.set('to-chain-select', select);
  const document = {
    addEventListener() {},
    createElement() { return {}; },
    querySelector() { return { style: {} }; },
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, { style: {}, value: '', textContent: '', innerHTML: '' });
      return elements.get(id);
    }
  };
  const source = dashboardSource.replace('return { init };',
    'return { applyChainCatalog, pairKeyFromIndex };');
  const context = vm.createContext({ document, App: { COIN: 10000000 } });
  vm.runInContext(`${source}\nthis.dashboard = SwapXFG;`, context);
  return { dashboard: context.dashboard, select };
}

test('numeric pair IDs resolve through the daemon catalog', () => {
  assert.ok(catalog.length >= 29, 'protocol catalog found');
  const { dashboard } = createDashboard();
  const chains = catalog.map(({ symbol, id }) => ({
    id, symbol, name: symbol, protocol: true, ready: id === 1
  }));
  assert.equal(dashboard.applyChainCatalog(chains), true);
  for (const { symbol, id } of catalog) {
    assert.equal(dashboard.pairKeyFromIndex(id), symbol, `pair ${id}`);
  }
  assert.equal(dashboard.pairKeyFromIndex(catalog.length), null);
});

test('unready pairs are disabled in the swap selector', () => {
  const { dashboard, select } = createDashboard();
  dashboard.applyChainCatalog([
    { id: 1, symbol: 'ETH', name: 'Ethereum', protocol: true, ready: true },
    { id: 24, symbol: 'ZANO', name: 'Zano', protocol: false, ready: false,
      implementation: 'staged' },
    { id: 28, symbol: 'DOT', name: 'Polkadot', protocol: false, ready: false,
      implementation: 'staged' }
  ]);
  assert.equal(select.options.find(option => option.value === 'ETH').disabled, false);
  for (const symbol of ['ZANO', 'DOT']) {
    assert.equal(select.options.find(option => option.value === symbol).disabled, true);
  }
});
