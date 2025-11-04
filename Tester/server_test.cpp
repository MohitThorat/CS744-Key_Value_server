#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <map>
#include <functional>
#include <curl/curl.h> // Requires libcurl-dev
#include "nlohmann/json.hpp" // Requires json.hpp in the same directory

// Use nlohmann's json library
using json = nlohmann::json;

const std::string BASE_URL = "http://127.0.0.1:8888";

/**
 * @brief This struct will hold the response from our HTTP requests.
 */
struct TestResponse {
    long code = 0;       // HTTP response code (e.g., 200, 201, 404)
    std::string body;  // The JSON response body as a string
    double time = 0.0;     // Total time for the request
};

/**
 * @brief Libcurl callback function to write response data into a std::string.
 */
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t new_length = size * nmemb;
    try {
        s->append(static_cast<char*>(contents), new_length);
    } catch(std::bad_alloc& e) {
        // Handle memory allocation failure
        return 0;
    }
    return new_length;
}

/**
 * @brief Performs an HTTP GET request.
 * @param url The full URL to GET.
 * @return A TestResponse struct.
 */
TestResponse http_get(const std::string& url) {
    TestResponse resp;
    std::string read_buffer;
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() failed");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &resp.time);
    } else {
        resp.body = curl_easy_strerror(res);
    }
    
    resp.body = read_buffer;
    curl_easy_cleanup(curl);
    return resp;
}

/**
 * @brief Performs an HTTP POST request with a JSON body.
 * @param url The full URL to POST to.
 * @param json_body The JSON string to send.
 * @return A TestResponse struct.
 */
TestResponse http_post(const std::string& url, const std::string& json_body) {
    TestResponse resp;
    std::string read_buffer;
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() failed");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &resp.time);
    } else {
        resp.body = curl_easy_strerror(res);
    }

    resp.body = read_buffer;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

/**
 * @brief Performs an HTTP DELETE request.
 * @param url The full URL to DELETE.
 * @return A TestResponse struct.
 */
TestResponse http_delete(const std::string& url) {
    TestResponse resp;
    std::string read_buffer;
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() failed");
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &resp.time);
    } else {
        resp.body = curl_easy_strerror(res);
    }
    
    resp.body = read_buffer;
    curl_easy_cleanup(curl);
    return resp;
}


// --- Test Definitions ---
// Each test function throws a std::runtime_error on failure.

void test_post_then_get() {
    std::string key = "test_key_1";
    std::string val = "hello_world_123";
    std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

    // 1. POST the key
    TestResponse post_resp = http_post(BASE_URL + "/key", body);
    if (post_resp.code != 201) {
        throw std::runtime_error("POST failed. Expected 201, got " + std::to_string(post_resp.code));
    }

    // 2. GET the key back
    TestResponse get_resp = http_get(BASE_URL + "/key?key=" + key);
    if (get_resp.code != 200) {
        throw std::runtime_error("GET failed. Expected 200, got " + std::to_string(get_resp.code));
    }

    // 3. VERIFY the body
    try {
        auto j = json::parse(get_resp.body);
        if (j["key"] != key || j["value"] != val) {
            throw std::runtime_error("GET body mismatch. Got: " + get_resp.body);
        }
    } catch (json::parse_error& e) {
        throw std::runtime_error("GET response was not valid JSON: " + get_resp.body);
    }
}

void test_get_nonexistent() {
    std::string key = "key_that_will_never_exist_abc123";
    TestResponse get_resp = http_get(BASE_URL + "/key?key=" + key);
    
    if (get_resp.code != 200) {
        throw std::runtime_error("GET non-existent failed. Expected 200, got " + std::to_string(get_resp.code));
    }

    // 2. VERIFY the body is empty
    try {
        auto j = json::parse(get_resp.body);
        if (j["key"] != key || j["value"] != "") {
            throw std::runtime_error("GET non-existent body mismatch. Expected empty value. Got: " + get_resp.body);
        }
    } catch (json::parse_error& e) {
        throw std::runtime_error("GET non-existent response was not valid JSON: " + get_resp.body);
    }
}

void test_post_delete_get() {
    std::string key = "test_key_to_delete";
    std::string val = "you_should_not_see_this";
    std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + val + "\"}";

    // 1. POST the key
    TestResponse post_resp = http_post(BASE_URL + "/key", body);
    if (post_resp.code != 201) {
        throw std::runtime_error("POST failed. Expected 201, got " + std::to_string(post_resp.code));
    }

    // 2. DELETE the key
    TestResponse del_resp = http_delete(BASE_URL + "/key/" + key);
    if (del_resp.code != 200) {
        throw std::runtime_error("DELETE failed. Expected 200, got " + std::to_string(del_resp.code));
    }

    // 3. GET the key (should be empty now)
    TestResponse get_resp = http_get(BASE_URL + "/key?key=" + key);
    if (get_resp.code != 200) {
        throw std::runtime_error("GET-after-DELETE failed. Expected 200, got " + std::to_string(get_resp.code));
    }

    // 4. VERIFY the body is empty
    try {
        auto j = json::parse(get_resp.body);
        if (j["key"] != key || j["value"] != "") {
            throw std::runtime_error("GET-after-DELETE body mismatch. Expected empty value. Got: " + get_resp.body);
        }
    } catch (json::parse_error& e) {
        throw std::runtime_error("GET-after-DELETE response was not valid JSON: " + get_resp.body);
    }
}

void test_post_update() {
    std::string key = "test_key_for_update";
    std::string val1 = "this_is_value_v1";
    std::string val2 = "this_is_the_NEW_value_v2";

    // 1. POST v1
    TestResponse post1_resp = http_post(BASE_URL + "/key", "{\"key\":\"" + key + "\",\"value\":\"" + val1 + "\"}");
    if (post1_resp.code != 201) {
        throw std::runtime_error("POST v1 failed. Expected 201, got " + std::to_string(post1_resp.code));
    }

    // 2. POST v2 (the update)
    TestResponse post2_resp = http_post(BASE_URL + "/key", "{\"key\":\"" + key + "\",\"value\":\"" + val2 + "\"}");
    if (post2_resp.code != 201) {
        throw std::runtime_error("POST v2 (update) failed. Expected 201, got " + std::to_string(post2_resp.code));
    }

    // 3. GET the key
    TestResponse get_resp = http_get(BASE_URL + "/key?key=" + key);
    if (get_resp.code != 200) {
        throw std::runtime_error("GET-after-update failed. Expected 200, got " + std::to_string(get_resp.code));
    }

    // 4. VERIFY it has v2
    try {
        auto j = json::parse(get_resp.body);
        if (j["key"] != key || j["value"] != val2) {
            throw std::runtime_error("GET-after-update body mismatch. Expected v2. Got: " + get_resp.body);
        }
    } catch (json::parse_error& e) {
        throw std::runtime_error("GET-after-update response was not valid JSON: " + get_resp.body);
    }
}


/**
 * @brief Simple test runner
 */
int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    std::map<std::string, std::function<void()>> tests;
    tests["Test 1: POST then GET (Create/Read)"] = test_post_then_get;
    tests["Test 2: GET non-existent key"] = test_get_nonexistent;
    tests["Test 3: POST, DELETE, then GET (Delete)"] = test_post_delete_get;
    tests["Test 4: POST then POST again (Update)"] = test_post_update;
    
    int passed = 0;
    int failed = 0;

    std::cout << "Starting server functional test...\n";
    std::cout << "Target: " << BASE_URL << "\n\n";

    for (const auto& test_pair : tests) {
        const std::string& test_name = test_pair.first;
        const auto& test_func = test_pair.second;

        std::cout << "--- " << test_name << " ---" << std::endl;
        try {
            test_func();
            std::cout << "[  PASS  ]\n" << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cout << "[  FAIL  ] - " << e.what() << "\n" << std::endl;
            failed++;
        }
    }

    std::cout << "\n--- Test Summary ---" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    curl_global_cleanup();

    return (failed > 0) ? 1 : 0; // Return 1 if any tests failed
}
