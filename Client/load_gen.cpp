#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <mutex>
#include <stdexcept>
#include <curl/curl.h>

// Rewritten load generator that reuses CURL handles per thread, enables keep-alive,
// minimizes handle creation/cleanup, and fixes success criteria.

std::atomic<long long> total_requests(0);
std::atomic<long long> total_response_time_us(0);
std::atomic<bool> stop_test(false);
std::atomic<long long> total_failed(0);
const std::string BASE_URL = "http://127.0.0.1:8888";
std::mutex popular_keys_mtx;
std::vector<std::string> popular_keys;
const int POPULAR_KEY_COUNT = 50;

std::string key_prefix = "";

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    // We’re discarding the response body since we don’t need it
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

std::string generate_long_key(int length) {
    std::string key;
    key.reserve(length);
    for (int i = 0; i < length; ++i) {
        key += 'k'; // simple repetitive character to reach desired length
    }
    return key;
}

std::string random_string(std::mt19937& gen, int length = 10) {
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> dist(0, (int)sizeof(chars) - 2);
    std::string s;
    s.reserve(length);
    for (int i = 0; i < length; i++)
        s.push_back(chars[dist(gen)]);
    return s;
}

// Helper wrappers to perform request using an existing CURL handle.
// They set only the options that change per-request; persistent options are
// configured once per-thread on initialization.

bool perform_get(CURL* curl, const std::string& url, long expected_code = 200) {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    long code = 0;
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    // if(res != CURLE_OK || code != expected_code) {
    //     std::cout<<"Failed Code: "<<code<<std::endl;
    //     std::cout<<"Failed curl Code: "<<res<<std::endl;
    // }
    return (res == CURLE_OK && code == expected_code);
}

bool perform_delete(CURL* curl, const std::string& url, long expected_code = 200) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    // restore back to GET (CURL retains CUSTOMREQUEST across calls sometimes)
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
    // if(res != CURLE_OK || code != expected_code) {
    //     std::cout<<"Failed Code: "<<code<<std::endl;
    //     std::cout<<"Failed curl Code: "<<res<<std::endl;
    // }
    return (res == CURLE_OK && code == expected_code);
}

bool perform_post(CURL* curl, const std::string& url, const std::string& json_body, struct curl_slist* headers, long expected_code = 201) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    // CURLOPT_POSTFIELDS expects the data to remain valid until the call returns,
    // which is true here because json_body is alive in this scope.
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)json_body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    // restore state: turn off POST so subsequent GET/DELETE are not affected
    curl_easy_setopt(curl, CURLOPT_POST, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    // if(res != CURLE_OK || code != expected_code) {
    //     std::cout<<"Failed Code: "<<code<<std::endl;
    //     std::cout<<"Failed curl Code: "<<res<<std::endl;
    // }
    return (res == CURLE_OK && code == expected_code);
}

void client_worker(const std::string& workload) {
    // Create one CURL handle per thread and reuse it for all requests.
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to init CURL handle in worker\n";
        return;
    }

    // Persistent options that speed up repeated calls and enable keep-alive
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // avoid using signals in libcurl
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 200L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    // Prepare a JSON header list once for POST requests
    struct curl_slist* json_headers = nullptr;
    json_headers = curl_slist_append(json_headers, "Content-Type: application/json");
    json_headers = curl_slist_append(json_headers, "Connection: keep-alive");

    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> pick(0, POPULAR_KEY_COUNT - 1);
    std::uniform_int_distribution<> mix(0, 99);

    while (!stop_test.load(std::memory_order_relaxed)) {
        auto start = std::chrono::steady_clock::now();
        bool ok = false;

        if (workload == "put-all") {
            std::string key = "key_" + random_string(gen, 12);
            std::string val = random_string(gen, 32);
            std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

            if (perform_post(curl, BASE_URL + "/key", body, json_headers, 201)) {
                ok = perform_delete(curl, BASE_URL + "/key/" + key, 200);
            }

        } else if (workload == "get-all") {
         //   std::cout<<"get-all worklaod"<<std::endl;
            std::string key = "miss_" + random_string(gen, 100);
            ok = perform_get(curl, BASE_URL + "/key?key=" + key, 200);

        } else if (workload == "get-popular") {
            std::string key;
            {
                std::lock_guard<std::mutex> lock(popular_keys_mtx);
                key = popular_keys[pick(gen) % popular_keys.size()];
            }
            ok = perform_get(curl, BASE_URL + "/key?key=" + key, 200);

        } else if (workload == "get-put") {
            int r = mix(gen);
            if (r < 80) {
                std::string key;
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[pick(gen) % popular_keys.size()];
                }
                ok = perform_get(curl, BASE_URL + "/key?key=" + key, 200);

            } else if (r < 95) {
                std::string key = "mix_" + random_string(gen, 12);
                std::string val = random_string(gen, 32);
                std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";
                ok = perform_post(curl, BASE_URL + "/key", body, json_headers, 201);

            } else {
                std::string key;
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[pick(gen) % popular_keys.size()];
                }
                ok = perform_delete(curl, BASE_URL + "/key/" + key, 200);
            }
        }

        if (ok) {
            auto end = std::chrono::steady_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_requests.fetch_add(1, std::memory_order_relaxed);
            total_response_time_us.fetch_add(us, std::memory_order_relaxed);
        } else {
            total_failed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (json_headers) curl_slist_free_all(json_headers);
    curl_easy_cleanup(curl);
}

void pre_populate_single_threaded() {
   //std::cout << "Pre-populating popular keys...\n";
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to init CURL for pre-populate\n";
        return;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Connection: keep-alive"); 
    std::string key_prefix = generate_long_key(16100) ;

    std::mt19937 gen(std::random_device{}());
    for (int i = 0; i < POPULAR_KEY_COUNT; ++i) {
        std::string key = key_prefix+ std::to_string(i);
        std::string val = random_string(gen, 16100);
        std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, (BASE_URL + "/key").c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        long code = 0;
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        // cleanup per-request state
        curl_easy_setopt(curl, CURLOPT_POST, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);

        if (res == CURLE_OK && code == 201) {
            std::lock_guard<std::mutex> lock(popular_keys_mtx);
            popular_keys.push_back(key);
        } else {
            std::cerr << "Failed prepopulate: " << key << " (code=" << code << ")\n";
        }
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    std::cout << "Done. Pre-populated " << popular_keys.size() << " keys.\n";
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cout << "Usage: ./load_gen <threads> <duration_secs> <workload>\n";
        std::cout << "workload: get-popular | get-all | put-all | get-put\n";
        return 1;
    }

    int threads = std::stoi(argv[1]);
    int duration = std::stoi(argv[2]);
    std::string workload = argv[3];

    // Initialize libcurl once per-process
    CURLcode g = curl_global_init(CURL_GLOBAL_ALL);
    if (g != CURLE_OK) {
        std::cerr << "curl_global_init failed\n";
        return 1;
    }

    if (workload == "get-popular" || workload == "get-put") {
        pre_populate_single_threaded();
        if (popular_keys.empty()) {
            std::cerr << "No popular keys inserted. Server down?\n";
            curl_global_cleanup();
            return 1;
        }
    }

    std::vector<std::thread> workers;

    // Start worker threads
    for (int i = 0; i < threads; ++i)
        workers.emplace_back(client_worker, workload);

    // Run for the requested duration
    std::this_thread::sleep_for(std::chrono::seconds(duration));
    stop_test.store(true);

    for (auto& t : workers) t.join();

    long long final = total_requests.load();
    long long total_us = total_response_time_us.load();

    double tps = (double)final / duration;
    double avg_rt = (final == 0) ? 0 : (double)total_us / final;

    std::cout << "\n--- Results for # thread ---"<<threads<<std::endl;
    std::cout << "Total Successful Requests: " << final << "\n";
    std::cout << "Duration:                 " << duration << "s\n";
    std::cout << "Throughput:               " << tps << " req/s\n";
    std::cout << "Avg Response Time:        " << avg_rt << " us\n";
    std::cout << "Total Failed:             " << total_failed.load() << "\n";

    curl_global_cleanup();
    return 0;
}
