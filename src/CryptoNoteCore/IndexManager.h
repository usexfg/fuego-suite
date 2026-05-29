// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>

#include "../../external/parallel_hashmap/phmap.h"
#include "../crypto/crypto.h"
#include "../crypto/hash.h"
#include "../Serialization/ISerializer.h"
#include "CryptoNoteSerialization.h"

using phmap::parallel_flat_hash_map;

namespace Common { class IInputStream; class IOutputStream; }
namespace CryptoNote {

class ISerializer;

struct BlockEntry;

struct TransactionIndex {
    uint32_t block;
    uint16_t transaction;

    void serialize(ISerializer& s) {
        s(block, "block");
        s(transaction, "tx");
    }
};

struct MultisignatureOutputUsage {
    TransactionIndex transactionIndex;
    uint16_t outputIndex;
    bool isUsed;

    void serialize(ISerializer& s) {
        s(transactionIndex, "txindex");
        s(outputIndex, "outindex");
        s(isUsed, "used");
    }
};

struct CommitmentOutputRef {
    TransactionIndex           transactionIndex;
    uint16_t                   outputInTransaction;
    Crypto::PublicKey          commitKey;
    uint32_t                   term;
    bool                       isSlashed = false;
    Crypto::EllipticCurvePoint commitment_ec;  // cached Pedersen commitment for ring verification

    void serialize(ISerializer& s) {
        s(transactionIndex, "txindex");
        s(outputInTransaction, "outindex");
        s(commitKey, "commitKey");
        s(term, "term");
        s(isSlashed, "is_slashed");
        s(commitment_ec, "commitment_ec");
    }
};


using key_images_container       = parallel_flat_hash_map<Crypto::KeyImage, uint32_t>;
using outputs_container          = parallel_flat_hash_map<uint64_t, std::vector<std::pair<TransactionIndex, uint16_t>>>;
using MultisignatureOutputsContainer = parallel_flat_hash_map<uint64_t, std::vector<MultisignatureOutputUsage>>;
using CommitmentOutputsContainer     = parallel_flat_hash_map<uint64_t, std::vector<CommitmentOutputRef>>;
using TransactionMap                 = parallel_flat_hash_map<Crypto::Hash, TransactionIndex>;

// ─── IIndex interface ─────────────────────────────────────────────────────
//
// Base interface for all blockchain indices.  Currently the interface exposes
// only clear(); concrete types are accessed by their specific IndexManager
// getters.  This is kept as a future-proofing anchor: when Phase 3 async
// background rebuild is implemented, the IndexManager can iterate over a
// collection of IIndex* to clear/reload all indices uniformly without
// enumerating each concrete type.

class IIndex {
public:
    virtual ~IIndex() = default;
    virtual void clear() = 0;
};

// ─── SpentKeyIndex ────────────────────────────────────────────────────────

class SpentKeyIndex : public IIndex {
public:
    SpentKeyIndex() = default;

    void clear() override { m_data.clear(); }

    key_images_container& data() { return m_data; }
    const key_images_container& data() const { return m_data; }

    auto find(const Crypto::KeyImage& ki) { return m_data.find(ki); }
    auto find(const Crypto::KeyImage& ki) const { return m_data.find(ki); }
    auto end() { return m_data.end(); }
    auto end() const { return m_data.end(); }
    bool contains(const Crypto::KeyImage& ki) const { return m_data.find(ki) != m_data.end(); }
    auto insert(const std::pair<Crypto::KeyImage, uint32_t>& p) { return m_data.insert(p); }
    auto insert(const Crypto::KeyImage& ki, uint32_t height) { return m_data.insert(std::make_pair(ki, height)); }
    size_t erase(const Crypto::KeyImage& ki) { return m_data.erase(ki); }

    // phmap binary I/O
    template<class Archive> void load(Archive& ar) { m_data.load(ar); }
    template<class Archive> void dump(Archive& ar) { m_data.dump(ar); }

private:
    key_images_container m_data;
};

// ─── OutputIndex ──────────────────────────────────────────────────────────

class OutputIndex : public IIndex {
public:
    OutputIndex() = default;

    void clear() override { m_data.clear(); }

    outputs_container& data() { return m_data; }
    const outputs_container& data() const { return m_data; }

    auto find(uint64_t amount) { return m_data.find(amount); }
    auto find(uint64_t amount) const { return m_data.find(amount); }
    auto end() { return m_data.end(); }
    auto end() const { return m_data.end(); }
    auto& operator[](uint64_t amount) { return m_data[amount]; }
    void erase(outputs_container::iterator it) { m_data.erase(it); }

    template<class S> void serialize(S& s, const std::string& name) { s(m_data, name); }

private:
    outputs_container m_data;
};

// ─── MultisigOutputIndex ──────────────────────────────────────────────────

class MultisigOutputIndex : public IIndex {
public:
    MultisigOutputIndex() = default;

    void clear() override { m_data.clear(); }

    MultisignatureOutputsContainer& data() { return m_data; }
    const MultisignatureOutputsContainer& data() const { return m_data; }

    auto find(uint64_t amount) { return m_data.find(amount); }
    auto find(uint64_t amount) const { return m_data.find(amount); }
    auto end() { return m_data.end(); }
    auto end() const { return m_data.end(); }
    auto& operator[](uint64_t amount) { return m_data[amount]; }
    void erase(MultisignatureOutputsContainer::iterator it) { m_data.erase(it); }

    template<class S> void serialize(S& s, const std::string& name) { s(m_data, name); }

private:
    MultisignatureOutputsContainer m_data;
};

// ─── CommitmentOutputIndex ────────────────────────────────────────────────

class CommitmentOutputIndex : public IIndex {
public:
    CommitmentOutputIndex() = default;

    void clear() override { m_data.clear(); }

    CommitmentOutputsContainer& data() { return m_data; }
    const CommitmentOutputsContainer& data() const { return m_data; }

    auto find(uint64_t amount) { return m_data.find(amount); }
    auto find(uint64_t amount) const { return m_data.find(amount); }
    auto end() { return m_data.end(); }
    auto end() const { return m_data.end(); }
    auto& operator[](uint64_t amount) { return m_data[amount]; }
    void erase(CommitmentOutputsContainer::iterator it) { m_data.erase(it); }

    template<class S> void serialize(S& s, const std::string& name) { s(m_data, name); }

private:
    CommitmentOutputsContainer m_data;
};

// ─── TransactionMapIndex ──────────────────────────────────────────────────

class TransactionMapIndex : public IIndex {
public:
    TransactionMapIndex() = default;

    void clear() override { m_data.clear(); }

    TransactionMap& data() { return m_data; }
    const TransactionMap& data() const { return m_data; }

    auto find(const Crypto::Hash& h) { return m_data.find(h); }
    auto find(const Crypto::Hash& h) const { return m_data.find(h); }
    auto end() { return m_data.end(); }
    auto end() const { return m_data.end(); }
    auto insert(const std::pair<Crypto::Hash, TransactionIndex>& p) { return m_data.insert(p); }
    auto insert(const Crypto::Hash& h, TransactionIndex idx) { return m_data.insert(std::make_pair(h, idx)); }
    size_t erase(const Crypto::Hash& h) { return m_data.erase(h); }
    size_t size() const { return m_data.size(); }
    TransactionIndex at(const Crypto::Hash& h) const { return m_data.at(h); }

    // phmap binary I/O
    template<class Archive> void load(Archive& ar) { m_data.load(ar); }
    template<class Archive> void dump(Archive& ar) { m_data.dump(ar); }

private:
    TransactionMap m_data;
};

// ─── IndexManager orchestration ──────────────────────────────────────────

class IndexManager {
public:
    IndexManager() = default;

    // ── Clear all indices ─────────────────────────────────────────────────
    void clear();

    // ── Async / readiness gate ────────────────────────────────────────────
    //
    // During rebuildCache() the flag is cleared, causing all RPC-facing
    // query methods to return safe defaults (false / 0 / empty) rather than
    // incomplete data.  This is intentionally indistinguishable from
    // "genuinely not found" — there are no callers that need to distinguish
    // "not ready" from "not present" because the rebuild always runs under
    // m_blockchain_lock, and no external client can observe the window.
    bool isReady() const { return m_ready.load(std::memory_order_acquire); }
    void setReady(bool ready) { m_ready.store(ready, std::memory_order_release); }

    // Lock held during rebuild; blocks concurrent queries.
    std::mutex& rebuildMutex() { return m_rebuildMutex; }

    // ── Per-index access ──────────────────────────────────────────────────
    SpentKeyIndex& spentKeys() { return m_spentKeys; }
    const SpentKeyIndex& spentKeys() const { return m_spentKeys; }

    OutputIndex& outputs() { return m_outputs; }
    const OutputIndex& outputs() const { return m_outputs; }

    MultisigOutputIndex& multisigOutputs() { return m_multisigOutputs; }
    const MultisigOutputIndex& multisigOutputs() const { return m_multisigOutputs; }

    CommitmentOutputIndex& commitmentOutputs() { return m_commitmentOutputs; }
    const CommitmentOutputIndex& commitmentOutputs() const { return m_commitmentOutputs; }


    TransactionMapIndex& transactionMap() { return m_transactionMap; }
    const TransactionMapIndex& transactionMap() const { return m_transactionMap; }

private:
    SpentKeyIndex         m_spentKeys;
    OutputIndex           m_outputs;
    MultisigOutputIndex   m_multisigOutputs;
    CommitmentOutputIndex m_commitmentOutputs;
    TransactionMapIndex   m_transactionMap;
    std::atomic<bool>     m_ready{false};
    std::mutex            m_rebuildMutex;
};

} // namespace CryptoNote
