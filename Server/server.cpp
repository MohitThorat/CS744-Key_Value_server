#include "CivetServer.h" // Use quotes because it's in our project directory
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include "LRUCache.h"
#include "MySQLHelper.h"
#include "MySQLPool.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;
#define cache_size 1024
LRUCache cache(cache_size);
MySQLPool mysql_pool("localhost", "root", "", "KVStore", 3306, 6);
#ifdef num_thread
const char *num_threads = "400";
#else
const char *num_threads = "1";
#endif
using namespace std;

// This handler will be called for all requests to /key
class ItemHandler : public CivetHandler
{
public:
    bool handleGet(CivetServer *server, struct mg_connection *conn) override
    {
        string key, response_body;
        if (server->getParam(conn, "key", key))
        {

            ostringstream oss;
            string value = cache.get(key);
            if (value.empty())
            {
                MYSQL *conn = mysql_pool.acquire();
                value = get_value(conn, key);
                mysql_pool.release(conn);

                if (!value.empty())
                { // Only cache if we found it
                    cache.put(key, value);
                }
            }

            oss << "{\"key\": \"" << key << "\", \"value\": \"" << value << "\"}";
            response_body = oss.str();
        }
        else
        {
            // Failure, 'id' was not found
            response_body = "{\"error\": \"No 'key' parameter was provided.\"}";
        }
        // 3. Send the response
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n\r\n",
                  (int)response_body.length());

        mg_write(conn, response_body.c_str(), response_body.length());

        return true; // We handled the request
    }

    bool handlePost(CivetServer *server, struct mg_connection *conn) override
    {
        long long content_length = mg_get_request_info(conn)->content_length;
        string post_data, key, value;
        post_data.resize(content_length);
        mg_read(conn, post_data.data(), content_length);

        try
        {
            // 1. PARSE: Attempt to parse the raw string into a JSON object
            auto json_data = nlohmann::json::parse(post_data);

            // 2. ACCESS: Get the field value (e.g., "username")
            key = json_data["key"].get<std::string>();
            value = json_data["value"].get<std::string>();
        }
        catch (...)
        {
            std::string err_resp = "{\"status\": \"error\", \"message\": \"Invalid JSON format\"}";
            mg_send_http_error(conn, 400, "Bad Request: Invalid JSON");
            return true;
        }

        // store in cache
        cache.put(key, value);
        // store in DB asynchronously
        auto key_hash = md5_hash(key);
        async_insert(mysql_pool, key, key_hash, value);

        // Send Success Response
        std::ostringstream oss;
        oss << "{\"status\": \"ok\", \"created_key\": \"" << key << "\"}";
        std::string response_body = oss.str();
        // = std::format("{{\"status\": \"ok\", \"create_key\": \"{}\"}}", key);

        mg_printf(conn, "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n", response_body.length());
        mg_write(conn, response_body.c_str(), response_body.length());

        return true;
    }
    bool handleDelete(CivetServer *server, struct mg_connection *conn) override
    {
        string key, response_body;

        // 1. Get the full URI (e.g., "/keys/2")
        const struct mg_request_info *ri = mg_get_request_info(conn);
        std::string uri = ri->request_uri;

        // 2. Extract the key from the URI path
        // We look for the last '/' and take everything after it.
        size_t last_slash_pos = uri.rfind('/');

        // Check if the URI is just "/keys" or if a key is present
        if (last_slash_pos == std::string::npos || last_slash_pos == uri.length() - 1)
        {
            mg_send_http_error(conn, 400, "Bad Request: No key specified in path.");
            return true;
        }

        std::string key_to_delete = uri.substr(last_slash_pos + 1);
        // synchronously remove from cache
        cache.remove(key_to_delete);

        // asynchronously remove from DB
        auto key_hash = md5_hash(key_to_delete);
        async_delete(mysql_pool, key_hash);

        std::ostringstream oss;
        oss << "{\"status\": \"ok\", \"message\": \"" << key_to_delete << " deleted successfully.\"}";
        response_body = oss.str();

        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n\r\n",
                  (int)response_body.length());

        mg_write(conn, response_body.c_str(), response_body.length());

        return true; // We handled the request
    }
};

int main(void)
{

    const char *options[] = {
        "listening_ports", "8888", "num_threads", num_threads,
        NULL};

    try
    {
        // Number of DB worker threads
        const int num_db_threads = 2; // heuristic

        for (int i = 0; i < num_db_threads; ++i)
        {
            std::thread(db_worker,std::ref(mysql_pool)).detach();
        }
        CivetServer server(options); // Server starts here

        ItemHandler h_item;
        server.addHandler("/key", h_item);

        std::cout << "C++ server running on port 8888." << std::endl;
        std::cout << "Press Enter to exit." << std::endl;
        getchar();
    }
    catch (const CivetException &e)
    {
        std::cerr << "Failed to start server: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}