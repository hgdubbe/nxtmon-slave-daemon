#include "output_http.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

static size_t discard_response(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

int post_json_payload(const config_t *cfg, const char *json_payload,
                      char *err_buf, size_t err_max) {
    if (!cfg || !json_payload || !cfg->master.host[0]) {
        snprintf(err_buf, err_max, "missing master host or payload");
        return -1;
    }

    int port = cfg->master.port > 0 ? cfg->master.port : 443;
    char url[CONFIG_MAX_STR * 2];
    if (port == 443) {
        snprintf(url, sizeof(url), "https://%s/api/telemetry", cfg->master.host);
    } else {
        snprintf(url, sizeof(url), "https://%s:%d/api/telemetry", cfg->master.host, port);
    }

    CURLcode global_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (global_rc != CURLE_OK) {
        snprintf(err_buf, err_max, "curl_global_init failed: %s",
                 curl_easy_strerror(global_rc));
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err_buf, err_max, "curl_easy_init failed");
        return -1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    char auth_header[CONFIG_MAX_STR + 32];
    if (cfg->master.token[0]) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
                 cfg->master.token);
        headers = curl_slist_append(headers, auth_header);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_payload));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        snprintf(err_buf, err_max, "curl failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        snprintf(err_buf, err_max, "master returned HTTP %ld", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return 0;
}
