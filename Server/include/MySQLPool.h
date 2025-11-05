#include <mysql/mysql.h> // MySQL C API
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#pragma once

class MySQLPool
{
public:
    MySQLPool(const std::string &host,
              const std::string &user,
              const std::string &password,
              const std::string &db,
              int port,
              size_t pool_size);
    ~MySQLPool();
    // Acquire a connection (wait if none available)
    MYSQL *acquire();
    // Release a connection back to the pool
    void release(MYSQL *conn);

private:
    std::vector<MYSQL *> connections_;
    std::mutex mtx_;
    std::condition_variable cv_connections;

    std::string host_, user_, password_, db_;
    int port_;
};
