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
MySQLPool mysql_pool("localhost", "root", "", "KVStore", 3306, 20);
#ifdef num_thread
const char *num_threads = "8";
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
            // In handleGet
            json j_response;
            j_response["key"] = key;
            string value = cache.get(key);
            if (value.empty())
            {
                MYSQL *conn = mysql_pool.acquire();
                value = get_value(conn, key);
                mysql_pool.release(conn);

                if (!value.empty())
                { // Only cache if we found it
                    cache.put(key, value);
                    j_response["value"] = value;
                }
                else{
                    j_response["error"] = "Key not found";
                }
            }
            else
            {
                j_response["value"] = value;
            }
            
            response_body = j_response.dump();
        }
        else
        {
            // Failure, 'id' was not found
            response_body = "{\"error\": \"No 'key' parameter was provided.\"}";
        }

        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n\r\n",
            response_body.size());

        mg_write(conn, response_body.data(), response_body.size());
        return true; // We handled the request
    }

    bool handlePost(CivetServer *server, struct mg_connection *conn) override
    {
        long long content_length = mg_get_request_info(conn)->content_length;
        string post_data, key, value;
        // --- FIX: VALIDATE THE CONTENT-LENGTH ---
        if (content_length <= 0)
        {
            // 411 Length Required is the correct HTTP response
            json j_error;
            j_error["status"] = "error";
            j_error["message"] = "Content-Length header is missing or invalid.";
            std::string err_resp = j_error.dump();

            mg_printf(conn,
                      "HTTP/1.1 411 Length Required\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      err_resp.size());
            mg_write(conn, err_resp.data(), err_resp.size());

            return true;
        }
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
            json j_error;
            j_error["status"] = "error";
            j_error["message"] = "Invalid JSON format";
            std::string err_resp = j_error.dump();

            mg_printf(conn,
                      "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      err_resp.size());
            
            mg_write(conn, err_resp.data(), err_resp.size());
            return true;
        }

        // store in cache
        cache.put(key, value);
        // store in DB asynchronously
        auto key_hash = md5_hash(key);
        async_insert(mysql_pool, key, key_hash, value);

        // Send Success Response
        // = std::format("{{\"status\": \"ok\", \"create_key\": \"{}\"}}", key);
        json j_response;
        j_response["status"] = "ok";
        j_response["created_key"] = key;
        string response_body = j_response.dump(); // Much faster than ostringstream

        mg_printf(conn,
                  "HTTP/1.1 201 Created\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %zu\r\n\r\n", // <-- Headers end here
                  response_body.size()); // <-- Pass the body as an argument
        
        mg_write(conn, response_body.data(), response_body.size());

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
            json j_error;
            j_error["status"] = "error";
            j_error["message"] = "No key specified in path";
            std::string err_resp = j_error.dump();

            mg_printf(conn,
                      "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      err_resp.size());
            mg_write(conn, err_resp.data(), err_resp.size());

            return true;
        }

        std::string key_to_delete = uri.substr(last_slash_pos + 1);
        // synchronously remove from cache
        cache.remove(key_to_delete);

        // asynchronously remove from DB
        auto key_hash = md5_hash(key_to_delete);
        async_delete(mysql_pool, key_hash);

        json j_response;
        j_response["status"] = "ok";
        j_response["key_to_delete"] = "deleted successfully";
        response_body = j_response.dump(); // Much faster than ostringstream

        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n" // <-- FIX
                  "Content-Type: application/json\r\n"
                  "Content-Length: %zu\r\n\r\n",
                  response_body.size());
        mg_write(conn, response_body.data(), response_body.size());

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
        const int num_db_threads = 10; // heuristic

        for (int i = 0; i < num_db_threads; ++i)
        {
            std::thread(db_worker, std::ref(mysql_pool)).detach();
        }
        CivetServer server(options); // Server starts here

        ItemHandler h_item;
        server.addHandler("/key*", h_item);

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