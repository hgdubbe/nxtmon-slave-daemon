#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

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

static int fetch_url_auth(const char *url, const char *auth_header, struct MemoryStruct *body, char *err_buf, size_t err_max) {
    CURL *curl_handle;
    CURLcode res;

    body->memory = malloc(1);
    body->size = 0;

    curl_handle = curl_easy_init();
    if (!curl_handle) {
        snprintf(err_buf, err_max, "curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)body);
    
    /* Since the API usually requires auth, we pass extra_val as a credential string: user:pass */
    if (auth_header && strlen(auth_header) > 0) {
        /* CURLOPT_USERPWD (10005) handles the base64 encoding internally */
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, auth_header); 
    }
    
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

    /* In a real deployment with self-signed certs or direct IP access */
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L); 
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L); 

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        snprintf(err_buf, err_max, "curl failed: %.100s", curl_easy_strerror(res));
        curl_easy_cleanup(curl_handle);
        return -1;
    }

    curl_easy_cleanup(curl_handle);
    return 0;
}

/* =========================================================================
 * TEST: nc_serverinfo_api
 * Uses the Nextcloud Serverinfo App API to pull JSON health status.
 * Config: hosts[0].url -> https://cloud.xyz/ocs/v2.php/apps/serverinfo/api/1.0/info?format=json
 * Config: extra_key="auth", extra_val="admin:apppassword"
 * ========================================================================= */
static int nc_serverinfo_api_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (cfg->host_count == 0 || strlen(cfg->hosts[0].url) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing API URL in hosts[0].url", sizeof(r->detail));
        return 0;
    }

    const char *auth = NULL;
    if (strcmp(cfg->extra_key, "auth") == 0) {
        auth = cfg->extra_val;
    }

    struct MemoryStruct body = {0};
    char err[256] = {0};

    if (fetch_url_auth(cfg->hosts[0].url, auth, &body, err, sizeof(err)) != 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, err, sizeof(r->detail));
        if (body.memory) free(body.memory);
        return 0;
    }

    /* We parse the JSON response manually via basic substring since we don't 
       need the full cJSON parser tree here.
       The serverinfo API returns: {"ocs":{"meta":{"status":"ok","statuscode":100,"message":"OK"},"data":{...}}} */
       
    char *status_code_pos = strstr(body.memory, "\"statuscode\":");
    if (status_code_pos) {
        int code = atoi(status_code_pos + 13);
        if (code == 100 || code == 200) {
            r->ok = 1; r->value = 1.0;
            safe_copy(r->unit, "status", sizeof(r->unit));
            safe_copy(r->detail, "Serverinfo API reports OK", sizeof(r->detail));
        } else {
            r->ok = 0; r->value = (double)code;
            safe_copy(r->unit, "status", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "Serverinfo API returned status %d", code);
        }
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Invalid JSON from Serverinfo API (statuscode not found)", sizeof(r->detail));
    }

    /* Bonus: Extract active users from the JSON payload: "activeUsers":{"last5minutes":1} */
    if (max_results >= 2) {
        char *users_pos = strstr(body.memory, "\"last5minutes\":");
        if (users_pos) {
            int users = atoi(users_pos + 15);
            test_result_t *r2 = &results[(*out_count)++];
            safe_copy(r2->type, "nc_api_users", CONFIG_MAX_STR);
            snprintf(r2->display_name, CONFIG_MAX_STR, "%.100s (Active Users)", cfg->display_name);
            r2->timestamp = r->timestamp;
            r2->ok = 1;
            r2->value = (double)users;
            safe_copy(r2->unit, "count", sizeof(r2->unit));
            snprintf(r2->detail, sizeof(r2->detail), "%d active users in last 5 min", users);
        }
    }

    free(body.memory);
    return 0;
}

const test_module_t mod_nc_serverinfo_api = { .type_name = "nc_serverinfo_api", .collect = nc_serverinfo_api_collect };

void register_nc_api_tests(void) {
    framework_register(&mod_nc_serverinfo_api);
}
