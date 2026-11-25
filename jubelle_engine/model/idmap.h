/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * IdMap<V>: a persistent (immutable, structurally shared) map from je::Id to V.
 *
 * Every change returns a new map that shares all untouched parts with the old one, so
 * keeping an old version (a snapshot) costs nothing until something changes, and a
 * snapshot can be read on any thread while the model is being edited (D-E5, D-E6).
 *
 * Structure: a 32-way hash trie with separate leaf and child bitmaps (the "CHAMP"
 * layout). The key is spread by a bijective 64-bit mix, so two different ids never have
 * the same hash and no collision lists are needed. Depth ≤ 13. Iteration order depends
 * only on the keys, so it is deterministic.
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "id.h"

namespace je::model {
namespace detail {
//! splitmix64's finaliser: a bijection on 64-bit values
constexpr uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

inline int popcount32(uint32_t v)
{
    // the instruction where the target has it (x86 builds without it call a slow library routine instead)
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__POPCNT__) || defined(__aarch64__) || defined(__ARM_NEON))
    return __builtin_popcount(v);
#else
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return int((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
#endif
}
}

template<typename V>
class IdMap
{
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;

    struct Leaf {
        uint64_t hash;
        Id key;
        V value;
    };

    struct Node {
        uint32_t leafMap = 0;    // slots holding a leaf
        uint32_t childMap = 0;   // slots holding a child node
        std::vector<Leaf> leaves;        // in slot order
        std::vector<NodePtr> children;   // in slot order
    };

    static constexpr int BITS = 5;
    static constexpr uint32_t MASK = (1u << BITS) - 1;
    static constexpr int MAX_SHIFT = 60;   // the last level uses the remaining 4 bits

    static uint32_t slotOf(uint64_t hash, int shift) { return uint32_t((hash >> shift) & MASK); }
    static int indexOf(uint32_t map, uint32_t bit) { return detail::popcount32(map & (bit - 1)); }

public:
    IdMap() = default;

    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    //! A map holding these entries, built in one pass (no path copying). Keys must be distinct
    //! (std::invalid_argument otherwise). The result is the same map repeated set() calls give.
    static IdMap fromEntries(std::vector<std::pair<Id, V> > entries)
    {
        std::vector<Leaf> leaves;
        leaves.reserve(entries.size());
        for (auto& e : entries) {
            leaves.push_back(Leaf { detail::mix64(e.first.value()), e.first, std::move(e.second) });
        }
        // slot order at every level = ascending order of the hash read 5 bits at a time from the low end
        std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) { return reversedSlots(a.hash) < reversedSlots(b.hash); });
        for (size_t i = 1; i < leaves.size(); ++i) {
            if (leaves[i].key == leaves[i - 1].key) {
                throw std::invalid_argument("je::model::IdMap::fromEntries: the same id twice");
            }
        }
        IdMap out;
        out.m_size = leaves.size();
        if (!leaves.empty()) {
            out.m_root = buildNode(leaves, 0, leaves.size(), 0);
        }
        return out;
    }

    //! nullptr if absent
    const V* find(Id key) const
    {
        const uint64_t hash = detail::mix64(key.value());
        const Node* node = m_root.get();
        int shift = 0;
        while (node) {
            const uint32_t bit = 1u << slotOf(hash, shift);
            if (node->leafMap & bit) {
                const Leaf& leaf = node->leaves[size_t(indexOf(node->leafMap, bit))];
                return leaf.key == key ? &leaf.value : nullptr;
            }
            if (!(node->childMap & bit)) {
                return nullptr;
            }
            node = node->children[size_t(indexOf(node->childMap, bit))].get();
            shift += BITS;
        }
        return nullptr;
    }

    bool contains(Id key) const { return find(key) != nullptr; }

    //! A new map with key set to value (inserted or replaced)
    [[nodiscard]] IdMap set(Id key, V value) const
    {
        const uint64_t hash = detail::mix64(key.value());
        bool added = false;
        IdMap out;
        out.m_root = setIn(m_root.get(), Leaf { hash, key, std::move(value) }, 0, added);
        out.m_size = m_size + (added ? 1 : 0);
        return out;
    }

    //! A new map without key (the same map if absent)
    [[nodiscard]] IdMap erase(Id key) const
    {
        if (!m_root) {
            return *this;
        }
        const uint64_t hash = detail::mix64(key.value());
        bool removed = false;
        NodePtr root = eraseIn(m_root, hash, key, 0, removed);
        if (!removed) {
            return *this;
        }
        IdMap out;
        // an empty root is dropped; a root never collapses into its single leaf (no parent to hold it)
        out.m_root = (root && (root->leafMap | root->childMap)) ? root : nullptr;
        out.m_size = m_size - 1;
        return out;
    }

    //! Calls fn(Id, const V&) for every entry, in a deterministic order (by hash)
    template<typename Fn>
    void forEach(Fn&& fn) const
    {
        if (m_root) {
            forEachIn(*m_root, fn);
        }
    }

    //! True if both maps are the same version (cheap identity test, not content equality)
    bool sameVersion(const IdMap& o) const { return m_root == o.m_root; }

    //! Nodes not shared with `other` (for tests and memory accounting)
    size_t nodesNotSharedWith(const IdMap& other) const
    {
        std::vector<const Node*> theirs;
        if (other.m_root) {
            collect(*other.m_root, theirs);
        }
        std::vector<const Node*> mine;
        if (m_root) {
            collect(*m_root, mine);
        }
        size_t count = 0;
        for (const Node* n : mine) {
            bool shared = false;
            for (const Node* t : theirs) {
                if (t == n) {
                    shared = true;
                    break;
                }
            }
            count += shared ? 0 : 1;
        }
        return count;
    }

private:
    static NodePtr setIn(const Node* node, Leaf&& leaf, int shift, bool& added)
    {
        auto copy = node ? std::make_shared<Node>(*node) : std::make_shared<Node>();
        const uint32_t bit = 1u << slotOf(leaf.hash, shift);

        if (copy->leafMap & bit) {
            const size_t i = size_t(indexOf(copy->leafMap, bit));
            if (copy->leaves[i].key == leaf.key) {
                copy->leaves[i].value = std::move(leaf.value);   // replace
                added = false;
                return copy;
            }
            // two different keys in one slot: push both one level down
            assert(shift < MAX_SHIFT);
            Leaf existing = std::move(copy->leaves[i]);
            copy->leaves.erase(copy->leaves.begin() + std::ptrdiff_t(i));
            copy->leafMap &= ~bit;
            NodePtr child = pairNode(std::move(existing), std::move(leaf), shift + BITS);
            copy->children.insert(copy->children.begin() + indexOf(copy->childMap, bit), std::move(child));
            copy->childMap |= bit;
            added = true;
            return copy;
        }
        if (copy->childMap & bit) {
            const size_t i = size_t(indexOf(copy->childMap, bit));
            copy->children[i] = setIn(copy->children[i].get(), std::move(leaf), shift + BITS, added);
            return copy;
        }
        copy->leaves.insert(copy->leaves.begin() + indexOf(copy->leafMap, bit), std::move(leaf));
        copy->leafMap |= bit;
        added = true;
        return copy;
    }

    //! The hash with its 5-bit slot groups in reverse order, so sorting by it sorts by slot at every level
    static uint64_t reversedSlots(uint64_t hash)
    {
        uint64_t out = 0;
        for (int shift = 0; shift <= MAX_SHIFT; shift += BITS) {
            const int width = shift == MAX_SHIFT ? 4 : BITS;
            out = (out << width) | ((hash >> shift) & ((1u << width) - 1));
        }
        return out;
    }

    static NodePtr buildNode(std::vector<Leaf>& leaves, size_t lo, size_t hi, int shift)
    {
        auto node = std::make_shared<Node>();
        size_t i = lo;
        while (i < hi) {
            const uint32_t slot = slotOf(leaves[i].hash, shift);
            size_t j = i + 1;
            while (j < hi && slotOf(leaves[j].hash, shift) == slot) {
                ++j;
            }
            if (j - i == 1) {
                node->leafMap |= 1u << slot;
                node->leaves.push_back(std::move(leaves[i]));
            } else {
                node->childMap |= 1u << slot;
                node->children.push_back(buildNode(leaves, i, j, shift + BITS));
            }
            i = j;
        }
        return node;
    }

    static NodePtr pairNode(Leaf&& a, Leaf&& b, int shift)
    {
        auto node = std::make_shared<Node>();
        const uint32_t sa = slotOf(a.hash, shift);
        const uint32_t sb = slotOf(b.hash, shift);
        if (sa == sb) {
            node->childMap = 1u << sa;
            node->children.push_back(pairNode(std::move(a), std::move(b), shift + BITS));
            return node;
        }
        node->leafMap = (1u << sa) | (1u << sb);
        if (sa < sb) {
            node->leaves.push_back(std::move(a));
            node->leaves.push_back(std::move(b));
        } else {
            node->leaves.push_back(std::move(b));
            node->leaves.push_back(std::move(a));
        }
        return node;
    }

    static NodePtr eraseIn(const NodePtr& node, uint64_t hash, Id key, int shift, bool& removed)
    {
        const uint32_t bit = 1u << slotOf(hash, shift);
        if (node->leafMap & bit) {
            const size_t i = size_t(indexOf(node->leafMap, bit));
            if (!(node->leaves[i].key == key)) {
                return node;
            }
            auto copy = std::make_shared<Node>(*node);
            copy->leaves.erase(copy->leaves.begin() + std::ptrdiff_t(i));
            copy->leafMap &= ~bit;
            removed = true;
            return copy;
        }
        if (!(node->childMap & bit)) {
            return node;
        }
        const size_t i = size_t(indexOf(node->childMap, bit));
        NodePtr child = eraseIn(node->children[i], hash, key, shift + BITS, removed);
        if (!removed) {
            return node;
        }
        auto copy = std::make_shared<Node>(*node);
        if (!child->childMap && detail::popcount32(child->leafMap) == 1) {
            // the child holds a single leaf: pull it up into this node
            copy->children.erase(copy->children.begin() + std::ptrdiff_t(i));
            copy->childMap &= ~bit;
            copy->leaves.insert(copy->leaves.begin() + indexOf(copy->leafMap, bit), child->leaves.front());
            copy->leafMap |= bit;
        } else if (!child->childMap && !child->leafMap) {
            copy->children.erase(copy->children.begin() + std::ptrdiff_t(i));
            copy->childMap &= ~bit;
        } else {
            copy->children[i] = std::move(child);
        }
        return copy;
    }

    template<typename Fn>
    static void forEachIn(const Node& node, Fn& fn)
    {
        // slot order, leaves and children interleaved, so the order depends only on the hashes
        size_t li = 0, ci = 0;
        for (uint32_t slot = 0; slot <= MASK; ++slot) {
            const uint32_t bit = 1u << slot;
            if (node.leafMap & bit) {
                const Leaf& leaf = node.leaves[li++];
                fn(leaf.key, leaf.value);
            } else if (node.childMap & bit) {
                forEachIn(*node.children[ci++], fn);
            }
        }
    }

    static void collect(const Node& node, std::vector<const Node*>& out)
    {
        out.push_back(&node);
        for (const NodePtr& c : node.children) {
            collect(*c, out);
        }
    }

    NodePtr m_root;
    size_t m_size = 0;
};
}
