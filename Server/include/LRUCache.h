#pragma once
#include <string>

class LRUCache {
public:
    explicit LRUCache(size_t total_capacity);
    ~LRUCache() = default;

    // Add or update a key
    void put(const std::string &key, const std::string &value);

    // Retrieve a value. Returns empty string on miss (same as your old API).
    std::string get(const std::string &key);

    // Remove an entry. Returns true if removed.
    bool remove(const std::string &key);

private:
    struct Impl;
    Impl* impl_; // Pimpl to keep header tidy
};
