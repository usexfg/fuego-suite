# IndexManager Refactor Plan

## 🎯 Goal
Decouple blockchain indexing logic from the `Blockchain` class into a dedicated `IndexManager` service. This will improve maintainability, allow for asynchronous indexing, and provide a structured way to manage different index types (spent keys, outputs, commitment indices).

## 🏗️ Architectural Shift

### Current State
- `Blockchain` owns all index maps (`m_outputs`, `m_spent_keys`, `m_transactionMap`, etc.).
- `Blockchain::rebuildCache()` is a monolithic function that clears all maps and iterates through the entire blockchain.
- Indexing is synchronous and blocks the main thread during startup or re-indexing.

### Target State
- `Blockchain` owns an `IndexManager` instance.
- `IndexManager` coordinates several specialized `IIndex` implementations.
- Indexing is abstracted, allowing for incremental updates and potential background processing.

---

## 🛠️ Implementation Details

### 1. The `IIndex` Interface
Define a generic interface for all blockchain indices.
```cpp
class IIndex {
public:
    virtual ~IIndex() = default;
    virtual void update(const Block& block) = 0;
    virtual void rollback(const Block& block) = 0;
    virtual void clear() = 0;
    virtual void serialize(ISerializer& s) = 0;
    virtual void deserialize(ISerializer& s) = 0;
};
```

### 2. Specialized Index Implementations
The following indices will be moved from `Blockchain` to their own classes:
- `SpentKeyIndex`: Manages `m_spent_keys`.
- `OutputIndex`: Manages `m_outputs` (KeyOutputs).
- `MultisigOutputIndex`: Manages `m_multisignatureOutputs`.
- `CommitmentOutputIndex`: Manages `m_commitmentOutputs`.
- `CommitmentIndex`: Manages the `m_commitmentIndex` (Heat/Cold).
- `TransactionMapIndex`: Manages `m_transactionMap`.

### 3. The `IndexManager` Orchestrator
The `IndexManager` will act as the central authority.
- **Ownership**: Holds a collection of `std::unique_ptr<IIndex>`.
- **Updating**: `IndexManager::update(const Block& block)` iterates through all registered indices and calls their `update` method.
- **Caching**: Integrates with `BlockCacheSerializer` to delegate the serialization of each index.

---

## 🚀 Execution Phases

### Phase 1: Interface & Wrapper (Low Risk)
- Create `IIndex` and `IndexManager` headers.
- Implement `IndexManager` as a wrapper around the existing `Blockchain` members (delegating calls).
- Ensure existing functionality is unchanged.

### Phase 2: Data Migration (Moderate Risk)
- Move the actual `parallel_flat_hash_map` structures from `Blockchain` into the specialized index classes.
- Update all `Blockchain` getters to retrieve data via `m_indexManager`.
- Update `rebuildCache` to call `m_indexManager->rebuildAll()`.

### Phase 3: Async & Incremental Optimization (High Value)
- Implement a background thread for `rebuildAll()`.
- Add a `isReady()` check to prevent RPC queries from returning incomplete data during a rebuild.
- Refine the `BlockCacheSerializer` to support partial index loads.

---

## ✅ Verification Strategy

### 1. State Consistency
- **Graph Comparison**: Run `graphify update .` before and after the refactor. The resulting `GRAPH_REPORT.md` must show the exact same number of nodes and edges.
- **Binary Compatibility**: Verify that the `.dat` files produced by `storeCache` remain compatible with the old version (or migrate them via a version bump).

### 2. Performance Benchmarks
- **Startup Time**: Measure time from process start to "Blockchain ready" state.
- **Memory Footprint**: Monitor RSS to ensure no memory leaks were introduced by the new service abstractions.

### 3. Functional Tests
- **Transaction Validation**: Ensure `check_tx_semantic` still correctly identifies double-spends and invalid mixins.
- **Symmetry**: Verify that `update` followed by `rollback` returns the index to the exact previous state.
