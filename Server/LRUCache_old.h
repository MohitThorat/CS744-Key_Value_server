#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <list> // Doubly-linked list
#include <mutex>

class LRUCache {
private:
    size_t max_size;
    std::mutex mtx_;
    std::list<std::string> lru_list; // Stores keys by recency
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> cache;
    // Helper function to update recency
    void move_to_front(const std::string& key);
public:
    LRUCache(size_t size);

    // Add or update a key-value pair
    void put(const std::string& key, const std::string& value);

    // Get a value from the cache
    std::string get(const std::string& key);

    bool remove(const std::string& key);
};