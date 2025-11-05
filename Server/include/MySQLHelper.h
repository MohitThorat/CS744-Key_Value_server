#include <mysql/mysql.h> // MySQL C API
#include <cstring>
#include <vector>
#include <openssl/md5.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <functional>
#include <queue>
#include "MySQLPool.h"
#pragma once

extern std::queue<std::function<void()>> db_queue;
extern std::mutex queue_mtx;
extern std::condition_variable cv_db_queue;

// returns 16-byte MD5 digest
std::vector<unsigned char> md5_hash(const std::string &key);
std::string get_value(MYSQL *conn, const std::string &key);

// Worker thread function
void db_worker(MySQLPool& pool);

// Enqueue insert operation
void async_insert(MySQLPool& pool,
                  const std::string& key,
                  const std::vector<unsigned char>& key_hash,
                  const std::string& value);

// Enqueue delete operation
void async_delete(MySQLPool& pool, const std::vector<unsigned char>& key_hash);