#include <mysql/mysql.h> // MySQL C API
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <functional>
#include <queue>
#include "MySQLPool.h"

MySQLPool::MySQLPool(const std::string &host,
                     const std::string &user,
                     const std::string &password,
                     const std::string &db,
                     int port,
                     size_t pool_size)
    : host_(host), user_(user), password_(password), db_(db), port_(port)
{
    for (size_t i = 0; i < pool_size; ++i)
    {
        MYSQL *conn = mysql_init(nullptr);
        if (!conn)
            throw std::runtime_error("mysql_init failed");

        if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(),
                                db.c_str(), port, nullptr, 0))
        {
            throw std::runtime_error(mysql_error(conn));
        }

        connections_.push_back(conn);
    }
}

MySQLPool::~MySQLPool()
{
    for (auto conn : connections_)
    {
        mysql_close(conn);
    }
}

// Acquire a connection (wait if none available)
MYSQL *MySQLPool::acquire()
{
    std::unique_lock<std::mutex> lock(mtx_);
    cv_connections.wait(lock, [this]
                        { return !connections_.empty(); });

    MYSQL *conn = connections_.back();
    connections_.pop_back();
    return conn;
}

// Release a connection back to the pool
void MySQLPool::release(MYSQL *conn)
{
    std::lock_guard<std::mutex> lock(mtx_);
    connections_.push_back(conn);
    cv_connections.notify_one();
}