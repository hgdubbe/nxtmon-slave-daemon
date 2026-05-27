#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* =========================================================================
 * TEST: service_status
 * Checks if a systemd unit is active via `systemctl is-active`.
 * Uses extra_key: "unit", extra_val: "haproxy.service"
 * ========================================================================= */
static int srv_status_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    if (strcmp(cfg->extra_key, "unit") != 0 || strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing or invalid 'unit' in extra_key/val", sizeof(r->detail));
        return 0;
    }

    /* Sanitization to prevent shell injection from the config file */
    for (size_t i = 0; i < strlen(cfg->extra_val); i++) {
        if (!isalnum(cfg->extra_val[i]) && cfg->extra_val[i] != '-' && cfg->extra_val[i] != '_' && cfg->extra_val[i] != '.') {
            r->ok = 0; r->value = 0;
            safe_copy(r->detail, "Invalid characters in unit name", sizeof(r->detail));
            return 0;
        }
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "systemctl is-active %.100s 2>/dev/null", cfg->extra_val);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to execute systemctl", sizeof(r->detail));
        return 0;
    }

    char out[64] = {0};
    if (fgets(out, sizeof(out)-1, fp) != NULL) {
        out[strcspn(out, "\r\n")] = 0; /* strip newline */
    }
    pclose(fp);

    if (strcmp(out, "active") == 0) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Unit %.100s is active", cfg->extra_val);
    } else {
        r->ok = 0; r->value = 0.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Unit %.100s is %s", cfg->extra_val, out[0] ? out : "unknown");
    }

    return 0;
}

const test_module_t mod_service_status = { .type_name = "service_status", .collect = srv_status_collect };

/* =========================================================================
 * TEST: service_pid
 * Checks if a process is running by reading /proc/[pid]/comm
 * Uses extra_key: "process", extra_val: "redis-server"
 * ========================================================================= */
static int srv_pid_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    if (strcmp(cfg->extra_key, "process") != 0 || strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing 'process' in extra_key/val", sizeof(r->detail));
        return 0;
    }

    DIR *dir = opendir("/proc");
    if (!dir) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot open /proc", sizeof(r->detail));
        return 0;
    }

    struct dirent *ent;
    int found_pid = 0;
    
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit(ent->d_name[0])) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/proc/%.50s/comm", ent->d_name);
        FILE *fp = fopen(path, "r");
        if (fp) {
            char comm[256] = {0};
            if (fgets(comm, sizeof(comm)-1, fp)) {
                comm[strcspn(comm, "\r\n")] = 0;
                if (strcmp(comm, cfg->extra_val) == 0) {
                    found_pid = atoi(ent->d_name);
                    fclose(fp);
                    break;
                }
            }
            fclose(fp);
        }
    }
    closedir(dir);

    if (found_pid > 0) {
        r->ok = 1; r->value = (double)found_pid;
        safe_copy(r->unit, "pid", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Process %.100s is running (PID %d)", cfg->extra_val, found_pid);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->unit, "pid", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Process %.100s not found", cfg->extra_val);
    }
    return 0;
}

const test_module_t mod_service_pid = { .type_name = "service_pid", .collect = srv_pid_collect };

/* =========================================================================
 * REGISTRY EXPORT
 * ========================================================================= */
void register_services_tests(void) {
    framework_register(&mod_service_status);
    framework_register(&mod_service_pid);
}
