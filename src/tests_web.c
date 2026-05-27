#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* Requires libcurl4-openssl-dev: gcc -lcurl */
#include <curl/curl.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* =========================================================================
 * HTTP FETCH HELPER (via libcurl)
 * ========================================================================= */
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

/* Fetches URL into body. Updates total_time if not NULL. */
static int fetch_url(const char *url, struct MemoryStruct *body, struct MemoryStruct *headers, double *total_time, char *err_buf, size_t err_max) {
    CURL *curl_handle;
    CURLcode res;

    body->memory = malloc(1);
    body->size = 0;
    if (headers) {
        headers->memory = malloc(1);
        headers->size = 0;
    }

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        snprintf(err_buf, err_max, "curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)body);
    
    if (headers) {
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)headers);
    }
    
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(err_buf, err_max, "curl failed: %.100s", curl_easy_strerror(res));
        curl_easy_cleanup(curl_handle);
        return -1;
    }

    if (total_time) {
        curl_easy_getinfo(curl_handle, CURLINFO_TOTAL_TIME, total_time);
    }

    curl_easy_cleanup(curl_handle);
    return 0;
}

/* Helper to parse an integer from plain text (e.g., "active processes: 5") */
static int extract_int_value(const char *text, const char *key, int *out_val) {
    char *pos = strstr(text, key);
    if (!pos) return 0;
    pos += strlen(key);
    while (*pos == ' ' || *pos == ':') pos++;
    *out_val = atoi(pos);
    return 1;
}

static int extract_double_value(const char *text, const char *key, double *out_val) {
    char *pos = strstr(text, key);
    if (!pos) return 0;
    pos += strlen(key);
    while (*pos == ' ' || *pos == ':') pos++;
    *out_val = atof(pos);
    return 1;
}

/* =========================================================================
 * TEST: web_type
 * Config: extra_key="expected", extra_val="nginx"
 * ========================================================================= */
static int web_type_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    struct MemoryStruct body = {0}, headers = {0};
    char err[256] = {0};
    const char *url = (cfg->host_count > 0 && strlen(cfg->hosts[0].url) > 0) ? cfg->hosts[0].url : "http://127.0.0.1/";

    if (fetch_url(url, &body, &headers, NULL, err, sizeof(err)) != 0) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory);
        if (headers.memory) free(headers.memory);
        return 0;
    }

    for (size_t i = 0; i < headers.size; i++) headers.memory[i] = tolower((unsigned char)headers.memory[i]);
    
    char expected[128];
    safe_copy(expected, cfg->extra_val, sizeof(expected));
    for (size_t i = 0; i < strlen(expected); i++) expected[i] = tolower((unsigned char)expected[i]);

    if (strstr(headers.memory, expected) != NULL) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Webserver matches (%.100s)", expected);
    } else {
        r->ok = 0; r->value = 0.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Server header did not contain '%.100s'", expected);
    }

    free(body.memory); free(headers.memory);
    return 0;
}

/* =========================================================================
 * TEST: php_fpm_workers
 * ========================================================================= */
static int php_fpm_workers_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    struct MemoryStruct body = {0}; char err[256] = {0};
    const char *url = (cfg->host_count > 0 && strlen(cfg->hosts[0].url) > 0) ? cfg->hosts[0].url : "http://127.0.0.1/status";

    if (fetch_url(url, &body, NULL, NULL, err, sizeof(err)) != 0) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory); return 0;
    }

    int active = 0, total = 0;
    int has_active = extract_int_value(body.memory, "active processes:", &active);
    int has_total = extract_int_value(body.memory, "total processes:", &total);

    if (has_active && has_total && total > 0) {
        r->ok = 1; r->value = (double)active / total;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%d / %d active workers", active, total);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to parse active/total processes", sizeof(r->detail));
    }

    free(body.memory); return 0;
}

/* =========================================================================
 * TEST: php_fpm_queue
 * ========================================================================= */
static int php_fpm_queue_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    struct MemoryStruct body = {0}; char err[256] = {0};
    const char *url = (cfg->host_count > 0 && strlen(cfg->hosts[0].url) > 0) ? cfg->hosts[0].url : "http://127.0.0.1/status";

    if (fetch_url(url, &body, NULL, NULL, err, sizeof(err)) != 0) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory); return 0;
    }

    int queue = 0, queue_len = 0;
    int has_q = extract_int_value(body.memory, "listen queue:", &queue);
    int has_qlen = extract_int_value(body.memory, "listen queue len:", &queue_len);

    if (has_q && has_qlen && queue_len > 0) {
        r->ok = 1; r->value = (double)queue / queue_len;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Queue size: %d / %d", queue, queue_len);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to parse queue metrics", sizeof(r->detail));
    }

    free(body.memory); return 0;
}

/* =========================================================================
 * TEST: php_opcache
 * ========================================================================= */
static int php_opcache_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 2) return -1;
    
    struct MemoryStruct body = {0}; char err[256] = {0};
    const char *url = (cfg->host_count > 0 && strlen(cfg->hosts[0].url) > 0) ? cfg->hosts[0].url : "http://127.0.0.1/opcache.php";

    if (fetch_url(url, &body, NULL, NULL, err, sizeof(err)) != 0) {
        *out_count = 1; test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = get_timestamp(); r->ok = 0; r->value = 0; 
        safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory); return 0;
    }

    double hit_rate = -1.0, free_mem = -1.0;
    extract_double_value(body.memory, "hit_rate:", &hit_rate);
    extract_double_value(body.memory, "free_memory:", &free_mem);

    long long ts = get_timestamp();
    *out_count = 0;

    if (hit_rate >= 0) {
        test_result_t *r1 = &results[(*out_count)++];
        safe_copy(r1->type, cfg->type, CONFIG_MAX_STR);
        snprintf(r1->display_name, CONFIG_MAX_STR, "%.100s (Hit Rate)", cfg->display_name);
        r1->timestamp = ts; r1->ok = 1; r1->value = hit_rate;
        safe_copy(r1->unit, "percentage", sizeof(r1->unit));
        snprintf(r1->detail, sizeof(r1->detail), "OPCache Hit Rate: %.2f%%", hit_rate);
    }
    if (free_mem >= 0) {
        test_result_t *r2 = &results[(*out_count)++];
        safe_copy(r2->type, cfg->type, CONFIG_MAX_STR);
        snprintf(r2->display_name, CONFIG_MAX_STR, "%.100s (Free Mem)", cfg->display_name);
        r2->timestamp = ts; r2->ok = 1; r2->value = free_mem;
        safe_copy(r2->unit, "bytes", sizeof(r2->unit));
        snprintf(r2->detail, sizeof(r2->detail), "OPCache Free Memory: %.0f bytes", free_mem);
    }

    if (*out_count == 0) {
        *out_count = 1; test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = ts; r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to parse OPCache output", sizeof(r->detail));
    }

    free(body.memory); return 0;
}

/* =========================================================================
 * TEST: web_response_time
 * ========================================================================= */
static int web_response_time_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0]; *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    struct MemoryStruct body = {0}; char err[256] = {0}; double total_time = 0.0;
    const char *url = (cfg->host_count > 0 && strlen(cfg->hosts[0].url) > 0) ? cfg->hosts[0].url : "http://127.0.0.1/";

    if (fetch_url(url, &body, NULL, &total_time, err, sizeof(err)) != 0) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory); return 0;
    }

    r->ok = 1; r->value = total_time * 1000.0; 
    safe_copy(r->unit, "ms", sizeof(r->unit));
    snprintf(r->detail, sizeof(r->detail), "Response time: %.2f ms", r->value);

    free(body.memory); return 0;
}

const test_module_t mod_web_type          = { .type_name = "web_type",          .collect = web_type_collect };
const test_module_t mod_php_fpm_workers   = { .type_name = "php_fpm_workers",   .collect = php_fpm_workers_collect };
const test_module_t mod_php_fpm_queue     = { .type_name = "php_fpm_queue",     .collect = php_fpm_queue_collect };
const test_module_t mod_php_opcache       = { .type_name = "php_opcache",       .collect = php_opcache_collect };
const test_module_t mod_web_response_time = { .type_name = "web_response_time", .collect = web_response_time_collect };

void register_web_tests(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    framework_register(&mod_web_type);
    framework_register(&mod_php_fpm_workers);
    framework_register(&mod_php_fpm_queue);
    framework_register(&mod_php_opcache);
    framework_register(&mod_web_response_time);
}
