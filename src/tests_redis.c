#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Depends on libhiredis-dev: gcc -lhiredis */
#include <hiredis/hiredis.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    if (max == 0) return;
    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* =========================================================================
 * CONNECTION HELPER
 * ========================================================================= */
static redisContext* redis_connect_helper(const test_entry_t *cfg, char *err_buf, size_t err_max) {
    const char *host = "127.0.0.1";
    int port = 6379;
    
    if (cfg->host_count > 0) {
        if (strlen(cfg->hosts[0].host) > 0) host = cfg->hosts[0].host;
        if (cfg->hosts[0].port > 0) port = cfg->hosts[0].port;
    }

    redisContext *c = redisConnect(host, port);
    if (c == NULL || c->err) {
        if (c) {
            snprintf(err_buf, err_max, "Redis connect error: %.100s", c->errstr);
            redisFree(c);
        } else {
            snprintf(err_buf, err_max, "Redis connect error: can't allocate redis context");
        }
        return NULL;
    }

    /* Handle authentication if provided in extra_val */
    if ((strcmp(cfg->extra_key, "password") == 0 ||
         strcmp(cfg->extra_key, "auth") == 0 ||
         cfg->extra_key[0] == '\0') &&
        strlen(cfg->extra_val) > 0) {
        redisReply *reply = (redisReply*)redisCommand(c, "AUTH %s", cfg->extra_val);
        if (reply == NULL) {
            snprintf(err_buf, err_max, "Redis AUTH failed: connection dropped");
            redisFree(c);
            return NULL;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            snprintf(err_buf, err_max, "Redis AUTH error: %.100s", reply->str);
            freeReplyObject(reply);
            redisFree(c);
            return NULL;
        }
        freeReplyObject(reply);
    }
    return c;
}

/* Helper to parse a key-value from the Redis INFO string (format: key:value\r\n) */
static int get_info_value(const char *info, const char *key, char *out_val, size_t max_len) {
    char search[128];
    snprintf(search, sizeof(search), "\r\n%s:", key);
    
    const char *pos = strstr(info, search);
    if (!pos) {
        /* Check if it's the very first line */
        snprintf(search, sizeof(search), "%s:", key);
        if (strncmp(info, search, strlen(search)) == 0) {
            pos = info;
        } else {
            return 0; /* Not found */
        }
    } else {
        pos += 2; /* Skip \r\n */
    }

    const char *start = pos + strlen(key) + 1; /* +1 for ':' */
    const char *end = strstr(start, "\r\n");
    if (!end) end = start + strlen(start);

    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    
    memcpy(out_val, start, len);
    out_val[len] = '\0';
    return 1;
}

/* Helper to get the full INFO string */
static char* redis_get_info(redisContext *c) {
    redisReply *reply = (redisReply*)redisCommand(c, "INFO");
    if (reply == NULL) return NULL;
    
    char *info_str = NULL;
    if (reply->type == REDIS_REPLY_STRING) {
        info_str = strdup(reply->str);
    }
    freeReplyObject(reply);
    return info_str;
}

/* =========================================================================
 * TEST: redis_sync_channel
 * Checks the replication channel status (master_link_status).
 * ========================================================================= */
static int redis_sync_channel_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    redisContext *c = redis_connect_helper(cfg, err, sizeof(err));
    if (!c) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail)); return 0;
    }

    char *info = redis_get_info(c);
    redisFree(c);

    if (!info) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to retrieve INFO", sizeof(r->detail)); return 0;
    }

    char role[64] = {0};
    get_info_value(info, "role", role, sizeof(role));

    if (strcmp(role, "master") == 0) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Node is Master (Sync Channel implicitly active)");
    } else if (strcmp(role, "slave") == 0) {
        char link_status[64] = {0};
        get_info_value(info, "master_link_status", link_status, sizeof(link_status));
        
        if (strcmp(link_status, "up") == 0) {
            r->ok = 1; r->value = 1.0;
            safe_copy(r->unit, "boolean", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "Sync Channel UP (Slave)");
        } else {
            r->ok = 0; r->value = 0.0;
            safe_copy(r->unit, "boolean", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "Sync Channel DOWN (Slave)");
        }
    } else {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Unknown role: %.100s", role);
    }

    free(info);
    return 0;
}

/* =========================================================================
 * TEST: redis_sync_status
 * Extracts the replication offset. The central master will use the 
 * offset+timestamp to calculate replication lag across nodes.
 * ========================================================================= */
static int redis_sync_status_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    redisContext *c = redis_connect_helper(cfg, err, sizeof(err));
    if (!c) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail)); return 0;
    }

    char *info = redis_get_info(c);
    redisFree(c);

    if (!info) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to retrieve INFO", sizeof(r->detail)); return 0;
    }

    char offset_str[64] = {0};
    if (get_info_value(info, "master_repl_offset", offset_str, sizeof(offset_str))) {
        r->ok = 1; 
        r->value = atof(offset_str);
        safe_copy(r->unit, "offset", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Replication Offset: %.100s", offset_str);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Could not find master_repl_offset", sizeof(r->detail));
    }

    free(info);
    return 0;
}

/* =========================================================================
 * TEST: redis_memory
 * Checks memory usage vs maxmemory limit.
 * ========================================================================= */
static int redis_memory_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    redisContext *c = redis_connect_helper(cfg, err, sizeof(err));
    if (!c) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail)); return 0;
    }

    char *info = redis_get_info(c);
    redisFree(c);
    if (!info) return 0;

    char used_str[64] = {0}, max_str[64] = {0};
    get_info_value(info, "used_memory", used_str, sizeof(used_str));
    get_info_value(info, "maxmemory", max_str, sizeof(max_str));

    unsigned long long used = strtoull(used_str, NULL, 10);
    unsigned long long max = strtoull(max_str, NULL, 10);

    if (max > 0) {
        r->ok = 1;
        r->value = (double)used / max;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%llu / %llu bytes used", used, max);
    } else {
        /* No limit defined */
        r->ok = 1;
        r->value = (double)used;
        safe_copy(r->unit, "bytes", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%llu bytes used (No maxmemory limit)", used);
    }

    free(info);
    return 0;
}

/* =========================================================================
 * TEST: redis_evicted_keys
 * Returns the total number of evicted keys.
 * ========================================================================= */
static int redis_evicted_keys_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    redisContext *c = redis_connect_helper(cfg, err, sizeof(err));
    if (!c) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail)); return 0;
    }

    char *info = redis_get_info(c);
    redisFree(c);
    if (!info) return 0;

    char evicted_str[64] = {0};
    if (get_info_value(info, "evicted_keys", evicted_str, sizeof(evicted_str))) {
        r->ok = 1;
        r->value = atof(evicted_str);
        safe_copy(r->unit, "count", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Evicted keys: %.100s", evicted_str);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Could not find evicted_keys", sizeof(r->detail));
    }

    free(info);
    return 0;
}

/* =========================================================================
 * TEST: redis_clients
 * Returns the number of connected clients.
 * ========================================================================= */
static int redis_clients_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    redisContext *c = redis_connect_helper(cfg, err, sizeof(err));
    if (!c) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail)); return 0;
    }

    char *info = redis_get_info(c);
    redisFree(c);
    if (!info) return 0;

    char clients_str[64] = {0};
    if (get_info_value(info, "connected_clients", clients_str, sizeof(clients_str))) {
        r->ok = 1;
        r->value = atof(clients_str);
        safe_copy(r->unit, "count", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Connected clients: %.100s", clients_str);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Could not find connected_clients", sizeof(r->detail));
    }

    free(info);
    return 0;
}

const test_module_t mod_redis_sync_channel = { .type_name = "redis_sync_channel", .collect = redis_sync_channel_collect };
const test_module_t mod_redis_sync_status  = { .type_name = "redis_sync_status",  .collect = redis_sync_status_collect };
const test_module_t mod_redis_memory       = { .type_name = "redis_memory",       .collect = redis_memory_collect };
const test_module_t mod_redis_evicted      = { .type_name = "redis_evicted_keys", .collect = redis_evicted_keys_collect };
const test_module_t mod_redis_clients      = { .type_name = "redis_clients",      .collect = redis_clients_collect };

/* =========================================================================
 * REGISTRY EXPORT
 * ========================================================================= */
void register_redis_tests(void) {
    framework_register(&mod_redis_sync_channel);
    framework_register(&mod_redis_sync_status);
    framework_register(&mod_redis_memory);
    framework_register(&mod_redis_evicted);
    framework_register(&mod_redis_clients);
}
