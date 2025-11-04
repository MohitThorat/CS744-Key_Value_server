#include <iostream>
#include <string>
#include <unordered_map>
#include <list> // Doubly-linked list
#include "LRUCache.h"
using namespace std;

int cache_hit;
LRUCache::LRUCache(size_t size) : max_size(size) {}

// Add or update a key-value pair
void LRUCache::put(const string &key, const string &value)
{
    std::lock_guard<std::mutex> lock(mtx_);
    // 1. Check if key already exists
    if (cache.count(key))
    {
        // Key exists: update its value
        cache[key].first = value;
        // Move it to the front of the "recently used" list
        move_to_front(key);
        
       
        return ;
    }

    // 2. Key is new. Check if cache is full.
    if (cache.size() >= max_size)
    {
        // Evict the "least recently used" item
        string lru_key = lru_list.back(); // Get key from back of list
        lru_list.pop_back();              // Remove from list
        cache.erase(lru_key);             // Remove from map
    }

    // 3. Add the new item
    lru_list.push_front(key);               // Add to front of "recently used" list
    cache[key] = {value, lru_list.begin()}; // Store value + iterator in map
}

// Get a value from the cache
string LRUCache::get(const string &key)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!cache.count(key))
    {

        return ""; // Cache miss
    }

    // Cache hit!
    // Move the accessed item to the front of the "recently used" list
    move_to_front(key);
    // Return the value
    return cache[key].first;
}

bool LRUCache::remove(const string &key)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!cache.count(key))
    {
        return false; // Key not found
    }
    lru_list.erase(cache[key].second);
    cache.erase(key);
    return true;
}

// Helper function to update recency
void LRUCache::move_to_front(const string &key)
{

    // 1. Erase from its current position in the list
    lru_list.erase(cache[key].second);
    // 2. Push to the front
    lru_list.push_front(key);
    // 3. Update the iterator in the map
    cache[key].second = lru_list.begin();
}
