'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const root = path.resolve(__dirname, '../..');
const header = fs.readFileSync(path.join(root, 'src/SwapDaemon/SwapTypes.h'), 'utf8');
const script = fs.readFileSync(path.join(root, 'dashboard/static/js/swapxfg.js'), 'utf8');

test('every numeric SwapPair ID resolves to its canonical chain key', () => {
  const enumBody = header.match(/enum class SwapPair\s*:\s*uint8_t\s*\{([\s\S]*?)\};/);
  assert.ok(enumBody, 'SwapPair enum must be present');

  const pairs = [...enumBody[1].matchAll(/^\s*([A-Z][A-Z0-9_]*)\s*=\s*(\d+)/gm)];
  assert.equal(pairs.length, 29, 'test should cover all current SwapPair values');

  const swapxfg = vm.runInNewContext(`${script}\nSwapXFG`, {
    document: { addEventListener() {} },
  });
  for (const [, name, id] of pairs) {
    assert.equal(swapxfg.pairKeyFromIndex(Number(id)), name, `pair ${id}`);
  }
  assert.equal(swapxfg.pairKeyFromIndex(29), null);
});
