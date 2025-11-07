#include <iostream>
#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include "LRUCache.h"

using namespace std;

// Constructor: Allocates shards using unique_ptr
LRUCache::LRUCache(size_t size) : total_max_size(size) {
    size_t shard_capacity = size / NUM_SHARDS;
    
    // Initialize the vector by constructing unique pointers in place.
    shards.reserve(NUM_SHARDS);
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        // Use make_unique to construct the Shard on the heap, 
        // storing a movable pointer in the vector.
        shards.push_back(std::make_unique<CacheShard>());
        
        // Configure the shard's capacity
        shards.back()->max_size_per_shard = shard_capacity;
    }
}

/**
 * @brief Computes the shard index for a given key.
 */
size_t LRUCache::get_shard_index(const string &key) const {
    size_t hash = std::hash<std::string>{}(key);
    return hash % NUM_SHARDS;
}

// Helper function now takes a pointer to the shard
void LRUCache::move_to_front_locked(CacheShard *shard, const string &key) {
    // Access members using -> (pointer access)
    shard->lru_list.erase(shard->cache[key].second);
    shard->lru_list.push_front(key);
    shard->cache[key].second = shard->lru_list.begin();
}

// Add or update a key-value pair
void LRUCache::put(const string &key, const string &value)
{
    size_t index = get_shard_index(key);
    CacheShard *shard = shards[index].get(); // Get the raw pointer to the shard

    // Lock ONLY the required shard!
    std::lock_guard<std::mutex> lock(shard->mtx); 
    
    if (shard->cache.count(key))
    {
        shard->cache[key].first = value;
        move_to_front_locked(shard, key);
        return ;
    }

    if (shard->cache.size() >= shard->max_size_per_shard)
    {
        string lru_key = shard->lru_list.back();
        shard->lru_list.pop_back();
        shard->cache.erase(lru_key);
    }

    shard->lru_list.push_front(key);
    shard->cache[key] = {value, shard->lru_list.begin()};
}

// Get a value from the cache
string LRUCache::get(const string &key)
{
    size_t index = get_shard_index(key);
    CacheShard *shard = shards[index].get(); // Get the raw pointer to the shard

    // Lock ONLY the required shard! 
    std::lock_guard<std::mutex> lock(shard->mtx);

    if (!shard->cache.count(key))
    {
        return ""; // Cache miss
    }

    // Cache hit! Update recency
    move_to_front_locked(shard, key);
    
    return shard->cache[key].first;
}

bool LRUCache::remove(const string &key)
{
    size_t index = get_shard_index(key);
    CacheShard *shard = shards[index].get(); // Get the raw pointer to the shard

    // Lock ONLY the required shard!
    std::lock_guard<std::mutex> lock(shard->mtx);
    
    if (!shard->cache.count(key))
    {
        return false; // Key not found
    }
    
    shard->lru_list.erase(shard->cache[key].second);
    shard->cache.erase(key);
    return true;
}
