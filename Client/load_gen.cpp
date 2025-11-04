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

std::atomic<long long> total_requests(0);
std::atomic<long long> total_response_time_us(0);
std::atomic<bool> stop_test(false);
std::atomic<long long> total_failed(0);
const std::string BASE_URL = "http://127.0.0.1:8888";
std::mutex popular_keys_mtx;
std::vector<std::string> popular_keys;
const int POPULAR_KEY_COUNT = 50;

static size_t write_callback(void*, size_t size, size_t nmemb, void*) {
    return size * nmemb;  // discard body (we don't need it)
}

std::string random_string(std::mt19937& gen, int length = 10) {
    static const char chars[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string s;
    s.reserve(length);
    for (int i = 0; i < length; i++)
        s.push_back(chars[dist(gen)]);
    return s;
}
bool http_post(const std::string& url, const std::string& json) {
    CURL* curl = curl_easy_init();
    if(!curl) return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    
    // THE FIX: Tell curl not to use signals
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    if(res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (code == 201);
}

bool http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if(!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    // THE FIX: Tell curl not to use signals
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    if(res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_easy_cleanup(curl);
    return (code == 200);
}

bool http_delete(const std::string& url) {
    CURL* curl = curl_easy_init();
    if(!curl) return false;

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    // THE FIX: Tell curl not to use signals
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    if(res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_easy_cleanup(curl);
    return (code == 200);
}

void client_worker(const std::string& workload) {
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> pick(0, POPULAR_KEY_COUNT - 1);
    std::uniform_int_distribution<> mix(0, 99);

    while(!stop_test) {
        auto start = std::chrono::steady_clock::now();
        bool ok = false;

        if(workload == "put-all") {
            std::string key = "key_" + random_string(gen, 12);
            std::string val = random_string(gen, 32);
            std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

            if(http_post(BASE_URL + "/key", body)) {
                ok = http_delete(BASE_URL + "/key/" + key);
            }

        } else if(workload == "get-all") {
            std::string key = "miss_" + random_string(gen, 12);
            ok = http_get(BASE_URL + "/key?key=" + key);

        } else if(workload == "get-popular") {
            std::string key;
            {
                std::lock_guard<std::mutex> lock(popular_keys_mtx);
                key = popular_keys[pick(gen)];
            }
            ok = http_get(BASE_URL + "/key?key=" + key);

        } else if(workload == "get-put") {
            int r = mix(gen);
            if(r < 80) {
                std::string key;
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[pick(gen)];
                }
                ok = http_get(BASE_URL + "/key?key=" + key);

            } else if(r < 95) {
                std::string key = "mix_" + random_string(gen, 12);
                std::string val = random_string(gen, 32);
                std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";
                ok = http_post(BASE_URL + "/key", body);

            } else {
                std::string key;
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[pick(gen)];
                }
                ok = http_delete(BASE_URL + "/key/" + key);
            }
        }

        if(ok) {
            auto end = std::chrono::steady_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_requests++;
            total_response_time_us += us;
        }
        else{
            total_failed++;
        }
    }
}

void pre_populate() {
    std::cout << "Pre-populating popular keys...\n";
    std::mt19937 gen(std::random_device{}());

    for(int i=0; i < POPULAR_KEY_COUNT; i++) {
        std::string key = "popular_" + std::to_string(i);
        std::string val = random_string(gen, 48);
        std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

        if(http_post(BASE_URL + "/key", body)) {
            popular_keys.push_back(key);
        } else {
            std::cerr << "Failed prepopulate: " << key << "\n";
        }
    }
    std::cout << "Done.\n";
}

int main(int argc, char** argv) {
    if(argc != 4) {
        std::cout << "Usage: ./load_gen <threads> <duration_secs> <workload>\n";
        return 1;
    }

    int threads = std::stoi(argv[1]);
    int duration = std::stoi(argv[2]);
    std::string workload = argv[3];

    curl_global_init(CURL_GLOBAL_ALL);

    if(workload == "get-popular" || workload == "get-put") {
        pre_populate();
        if(popular_keys.empty()) {
            std::cerr << "No popular keys inserted. Server down?\n";
            return 1;
        }
    }

    std::vector<std::thread> workers;
    for(int i = 0; i < threads; i++)
        workers.emplace_back(client_worker, workload);

    std::this_thread::sleep_for(std::chrono::seconds(duration));
    stop_test = true;

    for(auto& t : workers) t.join();

    long long final = total_requests.load();
    long long total_us = total_response_time_us.load();

    double tps = (double)final / duration;
    double avg_rt = (final == 0) ? 0 : (double)total_us / final;

    std::cout << "\n--- Results ---\n";
    std::cout << "Total Successful Requests: " << final << "\n";
    std::cout << "Duration:                 " << duration << "s\n";
    std::cout << "Throughput:               " << tps << " req/s\n";
    std::cout << "Avg Response Time:        " << avg_rt << " us\n";
    std::cout << "Total Failed:        " << total_failed << "\n";

    curl_global_cleanup();
    return 0;
}
