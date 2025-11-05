#include <iostream>
#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex> 
#include <memory> // For std::unique_ptr

#pragma once

// Configuration for sharding
const size_t NUM_SHARDS = 1024;

// Internal structure for each shard - REMAINS UNCHANGED
struct CacheShard {
    // This cannot be moved!
    std::mutex mtx; 
    
    // ... other members ...
    std::list<std::string> lru_list;
    // The pair stores: {value, list_iterator}
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> cache;
    size_t max_size_per_shard; 
};


class LRUCache
{
public:
    LRUCache(size_t size);
    void put(const std::string &key, const std::string &value);
    std::string get(const std::string &key);
    bool remove(const std::string &key);
    
private:
    // CRITICAL FIX: The vector now holds movable unique pointers.
    std::vector<std::unique_ptr<CacheShard>> shards;
    size_t total_max_size;

    size_t get_shard_index(const std::string &key) const;

    // We no longer pass the shard by reference; we pass the raw pointer.
    void move_to_front_locked(CacheShard *shard, const std::string &key);
};
