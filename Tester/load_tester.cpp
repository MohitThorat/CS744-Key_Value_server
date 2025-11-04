#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <mutex>
#include <stdexcept>
#include <curl/curl.h> // Requires libcurl-dev: sudo apt install libcurl4-openssl-dev

// --- Atomic counters for metrics ---
std::atomic<long long> total_successful_requests{0};
std::atomic<long long> total_failed_requests{0};
std::atomic<long long> total_response_time_us{0};
std::atomic<bool> stop_test{false};

// --- Server Configuration ---
const std::string BASE_URL = "http://127.0.0.1:8888";

// --- Workload-specific globals ---
std::mutex popular_keys_mtx;
std::vector<std::string> popular_keys;
const int POPULAR_KEY_COUNT = 50; // Number of keys to pre-populate for "popular" tests

/**
 * @brief A 'black hole' callback for libcurl to discard response bodies.
 */
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    (void)contents; // Suppress unused parameter warnings
    (void)userp;
    return size * nmemb; // Tell libcurl we "handled" this many bytes
}

/**
 * @brief Generates a random alphanumeric string.
 * @param gen Reference to a mersenne twister engine.
 * @param length The desired string length.
 * @return A random string.
 */
std::string random_string(std::mt19937& gen, int length = 10) {
    static const char chars[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string s;
    s.reserve(length);
    for (int i = 0; i < length; i++) {
        s.push_back(chars[dist(gen)]);
    }
    return s;
}

/**
 * @brief Performs an HTTP POST request with a JSON body.
 * @param url The full URL to POST to.
 * @param json_body The JSON string to send.
 * @return true if the server responded with HTTP 201 (Created), false otherwise.
 */
bool http_post(const std::string& url, const std::string& json_body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // Discard response
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L); // 2-second timeout

    long response_code = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (response_code == 201); // Your server returns 201 on POST
}

/**
 * @brief Performs an HTTP GET request.
 * @param url The full URL to GET.
 * @return true if the server responded with HTTP 200 (OK), false otherwise.
 */
bool http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    long response_code = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_easy_cleanup(curl);
    return (response_code == 200);
}

/**
 * @brief Performs an HTTP DELETE request.
 * @param url The full URL to DELETE.
 * @return true if the server responded with HTTP 200 (OK), false otherwise.
 */
bool http_delete(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    long response_code = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_easy_cleanup(curl);
    return (response_code == 200);
}

/**
 * @brief Main function for each client thread.
 * @param workload The name of the test to run (e.g., "get-all", "get-popular").
 */
void client_worker(const std::string& workload) {
    // Each thread gets its own random number generator
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> popular_picker(0, POPULAR_KEY_COUNT - 1);
    std::uniform_int_distribution<> workload_mixer(0, 99);

    while (!stop_test) {
        auto start = std::chrono::steady_clock::now();
        bool success = false;
        std::string key, val, body;

        if (workload == "get-all") {
            // 100% GETs on random keys (likely cache misses)
            key = "miss_" + random_string(gen, 12);
            success = http_get(BASE_URL + "/key?key=" + key);

        } else if (workload == "put-all") {
            // 100% PUTs (POST+DELETE)
            key = "key_" + random_string(gen, 12);
            val = random_string(gen, 32);
            body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";
            
            // We count success as BOTH operations succeeding
            if (http_post(BASE_URL + "/key", body)) {
                success = http_delete(BASE_URL + "/key/" + key);
            }

        } else if (workload == "get-popular") {
            // 100% GETs on a small set of popular keys (likely cache hits)
            {
                std::lock_guard<std::mutex> lock(popular_keys_mtx);
                key = popular_keys[popular_picker(gen)];
            }
            success = http_get(BASE_URL + "/key?key=" + key);

        } else if (workload == "get-put") {
            // Mixed workload: 80% GET (popular), 15% POST, 5% DELETE
            int r = workload_mixer(gen);

            if (r < 80) { // 80% GET
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[popular_picker(gen)];
                }
                success = http_get(BASE_URL + "/key?key=" + key);
            
            } else if (r < 95) { // 15% POST
                key = "mix_" + random_string(gen, 12);
                val = random_string(gen, 32);
                body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";
                success = http_post(BASE_URL + "/key", body);

            } else { // 5% DELETE
                {
                    std::lock_guard<std::mutex> lock(popular_keys_mtx);
                    key = popular_keys[popular_picker(gen)];
                }
                success = http_delete(BASE_URL + "/key/" + key);
            }
        }

        // Record metrics
        auto end = std::chrono::steady_clock::now();
        if (success) {
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_successful_requests++;
            total_response_time_us += us;
        } else {
            total_failed_requests++;
        }
    }
}

/**
 * @brief Pre-populates the server with a set of known keys for cache-hit tests.
 */
void pre_populate() {
    std::cout << "Pre-populating " << POPULAR_KEY_COUNT << " popular keys..." << std::endl;
    std::mt19937 gen(std::random_device{}());

    for (int i = 0; i < POPULAR_KEY_COUNT; i++) {
        std::string key = "popular_" + std::to_string(i);
        std::string val = random_string(gen, 48);
        std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

        if (http_post(BASE_URL + "/key", body)) {
            popular_keys.push_back(key);
        } else {
            std::cerr << "Failed to pre-populate key: " << key << std::endl;
        }
    }
    std::cout << "Pre-population complete. " << popular_keys.size() << " keys inserted." << std::endl;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: ./load_tester <threads> <duration_secs> <workload>\n";
        std::cerr << "Workloads:\n";
        std::cerr << "  get-all     (100% GET on random keys - cache miss)\n";
        std::cerr << "  put-all     (100% POST+DELETE new keys)\n";
        std::cerr << "  get-popular (100% GET on " << POPULAR_KEY_COUNT << " keys - cache hit)\n";
        std::cerr << "  get-put     (80% GET popular, 15% POST, 5% DELETE)\n";
        return 1;
    }

    int threads = 0;
    int duration = 0;
    try {
        threads = std::stoi(argv[1]);
        duration = std::stoi(argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Invalid threads or duration: " << e.what() << std::endl;
        return 1;
    }
    std::string workload = argv[3];

    curl_global_init(CURL_GLOBAL_ALL);

    // Pre-populate server if workload requires it
    if (workload == "get-popular" || workload == "get-put") {
        pre_populate();
        if (popular_keys.empty()) {
            std::cerr << "Pre-population failed. Is the server running at " << BASE_URL << "?\n";
            curl_global_cleanup();
            return 1;
        }
    }

    std::cout << "Starting load test with " << threads << " threads for " << duration << " seconds...\n";
    std::cout << "Workload: " << workload << "\n" << std::endl;

    // Start worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; i++) {
        workers.emplace_back(client_worker, workload);
    }

    // Wait for the test duration
    std::this_thread::sleep_for(std::chrono::seconds(duration));

    // Stop all threads
    stop_test = true;
    for (auto& t : workers) {
        t.join();
    }

    // --- Calculate and print results ---
    long long final_success = total_successful_requests.load();
    long long final_fail = total_failed_requests.load();
    long long total_us = total_response_time_us.load();
    long long total_ops = final_success + final_fail;

    double tps = (total_ops == 0) ? 0 : static_cast<double>(total_ops) / duration;
    double success_tps = (final_success == 0) ? 0 : static_cast<double>(final_success) / duration;
    double avg_rt = (final_success == 0) ? 0 : static_cast<double>(total_us) / final_success;

    std::cout << "\n--- Results ---" << std::endl;
    std::cout << "Workload:                 " << workload << std::endl;
    std::cout << "Threads:                  " << threads << std::endl;
    std::cout << "Duration:                 " << duration << "s" << std::endl;
    std::cout << "------------------" << std::endl;
    std::cout << "Total Requests:           " << total_ops << std::endl;
    std::cout << "Successful Requests:      " << final_success << std::endl;
    std::cout << "Failed Requests:          " << final_fail << std::endl;
    std::cout << "Total Throughput:         " << tps << " req/s" << std::endl;
    std::cout << "Success Throughput:       " << success_tps << " req/s" << std::endl;
    std::cout << "Avg Response Time (success): " << avg_rt << " ms" << std::endl;

    curl_global_cleanup();
    return 0;
}
