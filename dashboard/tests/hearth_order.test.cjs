const test = require('node:test');
const assert = require('node:assert/strict');

const HearthOrder = require('../static/js/hearth.js');

test('Hearth amounts remain exact in seven-decimal atomic units', () => {
  assert.equal(HearthOrder.atomic('1.0000001', 'amount'), 10000001);
  assert.equal(HearthOrder.formatAtomic(10000001), '1.0000001');
  assert.equal(HearthOrder.atomic('900719925.4740991', 'amount'), Number.MAX_SAFE_INTEGER);
  assert.throws(() => HearthOrder.atomic('0.00000001', 'amount'), /at most 7 decimals/);
  assert.throws(() => HearthOrder.atomic('900719925.4740992', 'amount'), /exact JSON range/);
  assert.throws(() => HearthOrder.atomic('0', 'amount'), /zero/);
});

test('live order amounts reject imprecise JSON numbers', () => {
  assert.equal(HearthOrder.isExactAtomic(Number.MAX_SAFE_INTEGER), true);
  assert.equal(HearthOrder.isExactAtomic(Number.MAX_SAFE_INTEGER + 1), false);
  assert.equal(HearthOrder.isExactAtomic('9007199254740992'), true);
  assert.equal(HearthOrder.isExactAtomic('1e12'), false);
});

test('buy XFG spends HEAT and sell XFG spends XFG', () => {
  const buy = HearthOrder.market(0, '2.5', { expected_output: 10000000 });
  assert.equal(buy.params.direction, 1);
  assert.equal(buy.params.input_amount, 25000000);
  assert.equal(buy.params.expected_output, 10000000);
  assert.equal(buy.params.min_output, 10000000);
  assert.equal(buy.outputAsset, 'XFG');
  const sell = HearthOrder.market(1, '2.5', { expected_output: 10000000 });
  assert.equal(sell.params.direction, 0);
  assert.equal(sell.outputAsset, 'HEAT');
  assert.throws(() => HearthOrder.market(0, '1', { expected_output: 0 }), /no exact/);
});

test('limit order units, tick and expiry follow wallet RPC contract', () => {
  const buy = HearthOrder.limit(0, '3.25', '0.10', '123456');
  assert.equal(buy.params.amount, 32500000); // HEAT budget, not XFG count
  assert.equal(buy.params.target_price, 1000000);
  assert.equal(buy.params.expiration, 123456); // absolute block height
  const sell = HearthOrder.limit(1, '3.25', '0.10', '0');
  assert.equal(sell.inputAsset, 'XFG');
  assert.throws(() => HearthOrder.limit(0, '1', '0.001', '0'), /multiple of 0.01/);
  assert.throws(() => HearthOrder.limit(0, '1', '0.01', '-1'), /Expiry/);
});

test('the Hearth preview executes the immutable validated wallet request', async () => {
  const originalDocument = global.document;
  const originalApp = global.App;
  const nodes = new Map();
  const node = id => {
    if (!nodes.has(id)) {
      nodes.set(id, {
        value: '', textContent: '', innerHTML: '', disabled: false,
        classList: { add() {}, remove() {} }
      });
    }
    return nodes.get(id);
  };
  node('order-amount').value = '2.5';
  node('order-price').value = '0.10';
  node('order-expiry').value = '123456';
  const sent = [];
  global.document = { getElementById: node };
  global.App = {
    showToast() {},
    async walletRpc(method, params) {
      sent.push({ method, params });
      return { tx_hash: 'a'.repeat(64) };
    }
  };
  try {
    await HearthOrder._ui.showOrderPreview();
    assert.match(node('order-modal-details').innerHTML, /2.5 HEAT/);
    // Changing the form after preview must not alter the signed request.
    node('order-amount').value = '100';
    await HearthOrder._ui.executeOrder();
    assert.equal(sent.length, 1);
    assert.equal(sent[0].method, 'place_limit_order');
    assert.equal(sent[0].params.amount, 25000000);
    assert.equal(sent[0].params.expiration, 123456);
  } finally {
    global.document = originalDocument;
    global.App = originalApp;
  }
});

test('quoted AMM preview uses HEAT to buy XFG and rechecks before wallet submission', async () => {
  const originalDocument = global.document;
  const originalApp = global.App;
  const originalChart = global.klinecharts;
  const originalArchive = global.XfgArchive;
  const originalInterval = global.setInterval;
  const nodes = new Map();
  const node = id => {
    if (!nodes.has(id)) {
      nodes.set(id, {
        value: '', textContent: '', innerHTML: '', disabled: false, style: {}, dataset: {},
        classList: { add() {}, remove() {}, toggle() {} },
        addEventListener(type, fn) { this[type] = fn; }
      });
    }
    return nodes.get(id);
  };
  const marketButton = node('market-button');
  marketButton.dataset.type = 'market';
  global.document = {
    getElementById(id) { return id === 'hearth-chart' ? null : node(id); },
    querySelectorAll(selector) { return selector === '.type-chip' ? [marketButton] : []; }
  };
  global.klinecharts = {
    init() { return { subscribeAction() {}, applyNewData() {} }; }
  };
  global.XfgArchive = { async load() {} };
  global.setInterval = () => 1;
  const sent = [];
  const quotes = [];
  let quoteOutput = 12500000;
  global.App = {
    COIN: 10000000,
    on() {},
    fmtXfg: String, fmtHeat: String, fmtPrice: String,
    showToast() {},
    async daemonPost(path, params) {
      if (path === '/amm_quote') {
        quotes.push(params);
        return { status: 'OK', expected_output: quoteOutput };
      }
      if (path === '/get_limit_orders') return { status: 'OK', orders: [] };
      if (path === '/amm_pool_info') return { status: 'OK', reserve_xfg: 1, reserve_heat: 1, spot_price: 1, height: 1 };
      if (path === '/heat_metrics') return { status: 'OK', heat_supply: 0, total_burned_xfg: 0 };
      throw new Error(path);
    },
    async walletRpc(method, params) {
      sent.push({ method, params });
      return { tx_hash: 'a'.repeat(64) };
    }
  };
  try {
    HearthOrder._ui.init();
    marketButton.click();
    node('order-amount').value = '2.5';
    await HearthOrder._ui.showOrderPreview();
    assert.match(node('order-modal-details').innerHTML, /1.25 XFG/);
    await HearthOrder._ui.executeOrder();
    assert.equal(quotes.length, 2);
    assert.deepEqual(quotes[0], { input_amount: 25000000, direction: 1 });
    const swaps = () => sent.filter(request => request.method === 'amm_swap');
    assert.equal(swaps().length, 1);
    assert.equal(swaps()[0].params.expected_output, 12500000);
    assert.equal(swaps()[0].params.min_output, 12500000);

    await HearthOrder._ui.showOrderPreview();
    quoteOutput = 12400000;
    await HearthOrder._ui.executeOrder();
    assert.equal(swaps().length, 1, 'worsened quote must not reach wallet RPC');
  } finally {
    global.document = originalDocument;
    global.App = originalApp;
    global.klinecharts = originalChart;
    global.XfgArchive = originalArchive;
    global.setInterval = originalInterval;
  }
});
