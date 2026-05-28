#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

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
 * TEST: nc_cron
 * Checks the timestamp of the Nextcloud cron execution.
 * Uses extra_val for the cron lockfile or a marker file
 * (e.g., /var/www/nextcloud/data/cron.lock).
 * ========================================================================= */
static int nc_cron_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing cron marker file path in extra_val", sizeof(r->detail));
        return 0;
    }

    struct stat st;
    if (stat(cfg->extra_val, &st) == 0) {
        long long diff = r->timestamp - st.st_mtime;
        
        r->ok = 1; 
        r->value = (double)diff;
        safe_copy(r->unit, "seconds", sizeof(r->unit));
        
        if (diff > 900) { /* Warning if older than 15 mins */
            snprintf(r->detail, sizeof(r->detail), "Cron last ran %lld seconds ago (Overdue!)", diff);
        } else {
            snprintf(r->detail, sizeof(r->detail), "Cron last ran %lld seconds ago", diff);
        }
    } else {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Failed to stat cron file: %.100s", cfg->extra_val);
    }

    return 0;
}

/* =========================================================================
 * TEST: nc_users
 * Uses occ CLI. Uses extra_val for the occ binary path.
 * ========================================================================= */
static int nc_users_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    const char *occ_path = strlen(cfg->extra_val) > 0 ? cfg->extra_val : "/var/www/nextcloud/occ";
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sudo -u www-data php %.200s user:info --output=json 2>/dev/null", occ_path);
       
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to execute occ command", sizeof(r->detail));
        return 0;
    }

    char buf[256];
    int success = 0;
    if (fgets(buf, sizeof(buf), fp) != NULL) {
        success = 1;
    }
    pclose(fp);

    if (success) {
        r->ok = 1; r->value = 1.0; /* Dummy value for generic occ execution */
        safe_copy(r->unit, "status", sizeof(r->unit));
        safe_copy(r->detail, "OCC command executed successfully", sizeof(r->detail));
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "OCC execution failed or returned no output", sizeof(r->detail));
    }

    return 0;
}

/* =========================================================================
 * TEST: nc_log_errors
 * Counts new "Error" or "Fatal" lines in nextcloud.log.
 * Uses extra_val for logfile path.
 * ========================================================================= */
static int nc_log_errors_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing logfile path in extra_val", sizeof(r->detail));
        return 0;
    }

    FILE *fp = fopen(cfg->extra_val, "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Cannot open logfile: %.100s", cfg->extra_val);
        return 0;
    }

    char lines[50][256];
    int line_idx = 0;
    char buf[256];
    
    while (fgets(buf, sizeof(buf), fp)) {
        snprintf(lines[line_idx % 50], sizeof(lines[line_idx % 50]), "%s", buf);
        line_idx++;
    }
    fclose(fp);

    int errors = 0;
    int start = (line_idx > 50) ? line_idx - 50 : 0;
    
    for (int i = start; i < line_idx; i++) {
        char *l = lines[i % 50];
        if (strstr(l, "\"level\":3") || strstr(l, "\"level\":4") || 
            strstr(l, "Error") || strstr(l, "Fatal")) {
            errors++;
        }
    }

    r->ok = 1;
    r->value = (double)errors;
    safe_copy(r->unit, "count", sizeof(r->unit));
    snprintf(r->detail, sizeof(r->detail), "Found %d errors in last 50 log lines", errors);

    return 0;
}

const test_module_t mod_nc_cron       = { .type_name = "nc_cron",       .collect = nc_cron_collect };
const test_module_t mod_nc_users      = { .type_name = "nc_users",      .collect = nc_users_collect };
const test_module_t mod_nc_log_errors = { .type_name = "nc_log_errors", .collect = nc_log_errors_collect };

void register_nc_tests(void) {
    framework_register(&mod_nc_cron);
    framework_register(&mod_nc_users);
    framework_register(&mod_nc_log_errors);
}
