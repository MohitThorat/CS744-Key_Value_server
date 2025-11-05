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

using namespace std;

queue<function<void()>> db_queue;
mutex queue_mtx;
condition_variable cv_db_queue;

// returns 16-byte MD5 digest
vector<unsigned char> md5_hash(const string &key)
{
    vector<unsigned char> digest(MD5_DIGEST_LENGTH);
    MD5((const unsigned char *)key.c_str(), key.size(), digest.data());
    return digest;
}
std::string get_value(MYSQL *conn, const std::string &key)
{
    auto hash = md5_hash(key);
    const char *query = "CALL select_kv(?)";

    // RAII wrapper for the statement
    MYSQL_STMT *stmt_handle = mysql_stmt_init(conn);
    if (!stmt_handle)
        throw std::runtime_error("mysql_stmt_init() failed");

    std::unique_ptr<MYSQL_STMT, void(*)(MYSQL_STMT*)> stmt_guard(stmt_handle, [](MYSQL_STMT* s){
        if(s) mysql_stmt_close(s);
    });
    MYSQL_STMT* stmt = stmt_guard.get(); // Use 'stmt' from now on

    // --- You can now safely throw exceptions ---

    if (mysql_stmt_prepare(stmt, query, strlen(query)))
        throw std::runtime_error(mysql_stmt_error(stmt));

    // --- Bind parameter (key_hash) ---
    MYSQL_BIND bind_param[1] = {0};
    bind_param[0].buffer_type = MYSQL_TYPE_BLOB;
    bind_param[0].buffer = (void *)hash.data();
    bind_param[0].buffer_length = hash.size();

    if (mysql_stmt_bind_param(stmt, bind_param))
        throw std::runtime_error(mysql_stmt_error(stmt));

    if (mysql_stmt_execute(stmt))
        throw std::runtime_error(mysql_stmt_error(stmt));

    // --- Prepare result binding ---
    MYSQL_BIND bind_result[1] = {0};
    unsigned long length = 0;
    std::vector<char> buffer(1024); // initial size

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = buffer.data();
    bind_result[0].buffer_length = buffer.size();
    bind_result[0].length = &length;

    if (mysql_stmt_bind_result(stmt, bind_result))
        throw std::runtime_error(mysql_stmt_error(stmt));

    // --- Fetch ---
    std::string result;
    int fetch_status = mysql_stmt_fetch(stmt);

    if (fetch_status == 0) // Success
    {
        if (length > buffer.size()) {
            buffer.resize(length);
            // Re-bind the buffer and fetch again
            bind_result[0].buffer = buffer.data();
            bind_result[0].buffer_length = buffer.size();
            // You might need to call mysql_stmt_bind_result again if required by the API
            // For fetch_column, it's simpler:
            mysql_stmt_fetch_column(stmt, &bind_result[0], 0, 0);
        }
        result.assign(buffer.data(), length);
    }
    else if (fetch_status == MYSQL_NO_DATA) // No row found
    {
        result = "";
    }
    else // An error
    {
        throw std::runtime_error(mysql_stmt_error(stmt));
    }

    // --- Cleanup ---
    mysql_stmt_free_result(stmt);
    // We NO LONGER call mysql_stmt_close(stmt) here.
    // The stmt_guard will do it automatically when the function returns.

    // Clear any remaining results from the stored procedure
    while (mysql_next_result(conn) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) mysql_free_result(res);
    }

    return result;
}
// Worker thread function
void db_worker(MySQLPool &pool)
{
    while (true)
    {
        function<void()> task;
        {
            unique_lock<mutex> lock(queue_mtx);
            cv_db_queue.wait(lock, []
                             { return !db_queue.empty(); });
            task = db_queue.front();
            db_queue.pop();
        }
        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "[DB Worker] Exception: %s\n", e.what());
        }
    }
}

// Enqueue insert operation
void async_insert(MySQLPool &pool,
                  const std::string &key,
                  const std::vector<unsigned char> &key_hash,
                  const std::string &value)
{
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        db_queue.push([pool_ptr = &pool, key, key_hash, value]
                      {
            MYSQL* conn = pool_ptr->acquire();
            if (!conn) {
                fprintf(stderr, "[DB Worker] Failed to acquire connection\n");
                return;
            }
            
            const char* query = "CALL insert_kv(?, ?, ?)";
            MYSQL_STMT* stmt = mysql_stmt_init(conn);
            if (!stmt)
                throw std::runtime_error("mysql_stmt_init() failed");
            if (mysql_stmt_prepare(stmt, query, strlen(query)))
                throw std::runtime_error(mysql_stmt_error(stmt));

            MYSQL_BIND bind[3] = {0};
            bind[0].buffer_type = MYSQL_TYPE_BLOB;
            bind[0].buffer = (void*)key_hash.data();
            bind[0].buffer_length = key_hash.size();

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (void*)key.c_str();
            bind[1].buffer_length = key.size();

            bind[2].buffer_type = MYSQL_TYPE_BLOB;
            bind[2].buffer = (void*)value.c_str();
            bind[2].buffer_length = value.size();

            if (mysql_stmt_bind_param(stmt, bind))
                throw std::runtime_error(mysql_stmt_error(stmt));

            if (mysql_stmt_execute(stmt))
                throw std::runtime_error(mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);

            pool_ptr->release(conn); });
    } // <-- lock_guard destroyed here, mutex released
    cv_db_queue.notify_one();
}

// Enqueue delete operation
void async_delete(MySQLPool &pool, const std::vector<unsigned char> &key_hash)
{
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        db_queue.push([pool_ptr = &pool, key_hash]
                      {
            MYSQL* conn = pool_ptr->acquire();
            if (!conn) {
                fprintf(stderr, "[DB Worker] Failed to acquire connection\n");
                return;
            }
            const char* query = "CALL delete_kv(?)";
            MYSQL_STMT* stmt = mysql_stmt_init(conn);
            if (!stmt)
                throw std::runtime_error("mysql_stmt_init() failed");
            if (mysql_stmt_prepare(stmt, query, strlen(query)))
                throw std::runtime_error(mysql_stmt_error(stmt));

            MYSQL_BIND bind[1] = {0};
            bind[0].buffer_type = MYSQL_TYPE_BLOB;
            bind[0].buffer = (void*)key_hash.data();
            bind[0].buffer_length = key_hash.size();

            if (mysql_stmt_bind_param(stmt, bind))
                throw std::runtime_error(mysql_stmt_error(stmt));

            if (mysql_stmt_execute(stmt))
                throw std::runtime_error(mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);

            pool_ptr->release(conn); });
    } // <-- lock_guard destroyed here, mutex released
    cv_db_queue.notify_one();
}