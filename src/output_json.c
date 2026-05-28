#include "config.h"
#include "framework.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Requires libcjson-dev: gcc -lcjson */
#include <cjson/cJSON.h>

/* =========================================================================
 * JSON PAYLOAD GENERATOR
 * Serializes the array of test_result_t into the Schema Version 2 format
 * as defined in the readme.md specification.
 * ========================================================================= */
char* generate_json_payload(const config_t *cfg, const test_result_t *results, size_t count) {
    if (!cfg || !results) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* 1. Root Level Metadata */
    cJSON_AddNumberToObject(root, "schema_version", 2);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

    /* 2. Agent Metadata Block */
    cJSON *agent = cJSON_CreateObject();
    if (agent) {
        cJSON_AddStringToObject(agent, "display_name", cfg->agent.display_name);
        
        char hostname[256] = "unknown";
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            snprintf(hostname, sizeof(hostname), "%s", "unknown");
        }
        cJSON_AddStringToObject(agent, "hostname", hostname);
        
        cJSON_AddItemToObject(root, "agent", agent);
    }

    /* 3. Results Array */
    cJSON *res_array = cJSON_CreateArray();
    if (res_array) {
        for (size_t i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            if (!item) continue;

            cJSON_AddStringToObject(item, "type", results[i].type);
            cJSON_AddStringToObject(item, "display_name", results[i].display_name);
            
            /* Handle the ternary 'ok' status (1=success, 0=fail, -1=unknown) */
            if (results[i].ok == 1) {
                cJSON_AddBoolToObject(item, "ok", 1);
            } else if (results[i].ok == 0) {
                cJSON_AddBoolToObject(item, "ok", 0);
            } else {
                cJSON_AddItemToObject(item, "ok", cJSON_CreateNull());
            }

            cJSON_AddNumberToObject(item, "value", results[i].value);

            /* Handle unit (null if empty) */
            if (strlen(results[i].unit) > 0) {
                cJSON_AddStringToObject(item, "unit", results[i].unit);
            } else {
                cJSON_AddItemToObject(item, "unit", cJSON_CreateNull());
            }

            cJSON_AddStringToObject(item, "detail", results[i].detail);
            cJSON_AddNumberToObject(item, "timestamp", (double)results[i].timestamp);

            cJSON_AddItemToArray(res_array, item);
        }
        cJSON_AddItemToObject(root, "results", res_array);
    }

    /* Serialize to minified string for network transmission */
    char *json_str = cJSON_PrintUnformatted(root);
    
    /* Clean up the JSON object tree */
    cJSON_Delete(root);
    
    return json_str; /* Caller must free() this pointer when network dispatch is done */
}
