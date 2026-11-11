// Compiles the swap timelock contracts with the pinned solc and writes the
// creation and runtime bytecode that xfg-swapd pins (EthRpcClient.h).
// `--check` compiles and fails if the committed artifacts differ.
const fs = require('fs');
const path = require('path');
const solc = require('solc');

const ETHEREUM_DIR = path.join(__dirname, '..', '..', 'src', 'SwapDaemon', 'Ethereum');
const CONTRACTS = ['HashedTimelock', 'PointTimelock'];
const SETTINGS = {
  optimizer: { enabled: true, runs: 200 },
  evmVersion: 'paris',
  outputSelection: { '*': { '*': ['evm.bytecode.object', 'evm.deployedBytecode.object'] } },
};

function compile(name) {
  const unit = `${name}.sol`;
  const input = {
    language: 'Solidity',
    sources: { [unit]: { content: fs.readFileSync(path.join(ETHEREUM_DIR, unit), 'utf8') } },
    settings: SETTINGS,
  };
  const output = JSON.parse(solc.compile(JSON.stringify(input)));
  const errors = (output.errors || []).filter((e) => e.severity === 'error');
  if (errors.length) throw new Error(errors.map((e) => e.formattedMessage).join('\n'));
  const contract = output.contracts[unit][name];
  return { creation: contract.evm.bytecode.object, runtime: contract.evm.deployedBytecode.object };
}

function artifactPaths(name) {
  const base = path.join(ETHEREUM_DIR, `${name}_sol_${name}`);
  return { creation: `${base}.bin`, runtime: `${base}.bin-runtime` };
}

const check = process.argv.includes('--check');
let mismatch = false;
console.log(`solc ${solc.version()}`);
for (const name of CONTRACTS) {
  const built = compile(name);
  const files = artifactPaths(name);
  for (const kind of ['creation', 'runtime']) {
    if (check) {
      const committed = fs.existsSync(files[kind]) ? fs.readFileSync(files[kind], 'utf8').trim() : '';
      const same = committed === built[kind];
      mismatch = mismatch || !same;
      console.log(`${same ? 'ok  ' : 'DIFF'} ${path.basename(files[kind])}`);
    } else {
      fs.writeFileSync(files[kind], `${built[kind]}\n`);
      console.log(`wrote ${path.basename(files[kind])} (${built[kind].length / 2} bytes)`);
    }
  }
}
process.exit(mismatch ? 1 : 0);
