#include "LRUCache.h"

#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <atomic>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>

// Tunables
static constexpr size_t NUM_SHARDS = 32;      // adjust to number of cores (power of two is nice)
static constexpr size_t SAMPLE_SIZE = 8;      // number of random samples on eviction

// Implementation details hidden in Impl
struct LRUCache::Impl {
    struct Entry {
        std::string value;
        std::atomic<uint64_t> last_access;
        Entry() = default;
        Entry(std::string v, uint64_t t) : value(std::move(v)), last_access(t) {}
    };

    struct Shard {
        std::unordered_map<std::string, Entry> map;
        std::shared_mutex mutex;       // shared for reads, exclusive for writes/evict
        size_t max_size = 0;
    };

    std::vector<std::unique_ptr<Shard>> shards;
    std::atomic<uint64_t> tick{1};    // monotonic counter for last_access
    std::mt19937_64 rng;

    Impl(size_t total_capacity) {
        // Seed RNG
        rng.seed(std::chrono::steady_clock::now().time_since_epoch().count());

        // Create shards and distribute capacity
        shards.reserve(NUM_SHARDS);
        size_t per = (total_capacity + NUM_SHARDS - 1) / NUM_SHARDS;
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            auto s = std::make_unique<Shard>();
            s->max_size = per;
            shards.emplace_back(std::move(s));
        }
    }

    size_t shard_index_for(const std::string &key) const {
        static std::hash<std::string> hasher;
        return hasher(key) % shards.size();
    }

    uint64_t now_tick() {
        // Increment and return current tick (relaxed is fine for ordering)
        return tick.fetch_add(1, std::memory_order_relaxed);
    }

    // Evict one entry from shard using random sampling. Caller MUST hold exclusive lock.
    void evict_sampled(Shard &shard) {
        if (shard.map.empty()) return;

        // Build a vector of pointers to keys to sample from
        size_t map_size = shard.map.size();
        size_t k = std::min<size_t>(SAMPLE_SIZE, map_size);

        // If small map, just find the oldest by full scan (cheap)
        if (map_size <= k + 2) {
            auto victim_it = shard.map.begin();
            uint64_t min_ts = UINT64_MAX;
            for (auto it = shard.map.begin(); it != shard.map.end(); ++it) {
                uint64_t ts = it->second.last_access.load(std::memory_order_relaxed);
                if (ts < min_ts) { min_ts = ts; victim_it = it; }
            }
            shard.map.erase(victim_it);
            return;
        }

        // Otherwise sample k random indices
        std::uniform_int_distribution<size_t> dist(0, map_size - 1);
        // To efficiently get random elements from unordered_map we advance iterator.
        // This is O(k + avg_advance), acceptable for moderate sample sizes.
        uint64_t oldest = UINT64_MAX;
        std::string oldest_key;
        for (size_t i = 0; i < k; ++i) {
            size_t idx = dist(rng);
            auto it = shard.map.begin();
            std::advance(it, idx);
            uint64_t ts = it->second.last_access.load(std::memory_order_relaxed);
            if (ts < oldest) {
                oldest = ts;
                oldest_key = it->first;
            }
        }
        if (!oldest_key.empty()) shard.map.erase(oldest_key);
    }
};

// ------------------- Public API Implementation -------------------

LRUCache::LRUCache(size_t total_capacity) : impl_(nullptr) {
    impl_ = new Impl(total_capacity);
}

void LRUCache::put(const std::string &key, const std::string &value) {
    size_t idx = impl_->shard_index_for(key);
    auto &shard = *impl_->shards[idx];

    // Exclusive lock for put + possible eviction
    std::unique_lock<std::shared_mutex> lock(shard.mutex);

    auto it = shard.map.find(key);
    uint64_t t = impl_->now_tick();
    if (it != shard.map.end()) {
        // update
        it->second.value = value;
        it->second.last_access.store(t, std::memory_order_relaxed);
        return;
    }

    // insert
    shard.map.try_emplace(key, value,t);

    // eviction if needed
    if (shard.map.size() > shard.max_size) {
        impl_->evict_sampled(shard);
    }
}

std::string LRUCache::get(const std::string &key) {
    size_t idx = impl_->shard_index_for(key);
    auto &shard = *impl_->shards[idx];

    // Shared lock for read
    std::shared_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.map.find(key);
    if (it == shard.map.end()) return "";

    // update last_access (atomic store) — still inside shared lock
    it->second.last_access.store(impl_->now_tick(), std::memory_order_relaxed);
    return it->second.value;
}

bool LRUCache::remove(const std::string &key) {
    size_t idx = impl_->shard_index_for(key);
    auto &shard = *impl_->shards[idx];

    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.map.find(key);
    if (it == shard.map.end()) return false;
    shard.map.erase(it);
    return true;
}
