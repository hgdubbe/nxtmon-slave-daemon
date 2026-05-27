#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700
#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* =========================================================================
 * TEST: ssl_cert
 * Checks SSL certificate expiration days remaining for a given domain.
 * Uses extra_key: "domain", extra_val: "example.com"
 * Requires `openssl` binary in PATH.
 * ========================================================================= */
static int ssl_cert_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    if (strcmp(cfg->extra_key, "domain") != 0 || strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing 'domain' in extra_key/val", sizeof(r->detail));
        return 0;
    }

    /* Basic sanitization (prevent shell injection) */
    for (size_t i = 0; i < strlen(cfg->extra_val); i++) {
        if (!isalnum(cfg->extra_val[i]) && cfg->extra_val[i] != '-' && cfg->extra_val[i] != '.') {
            r->ok = 0; r->value = 0;
            safe_copy(r->detail, "Invalid characters in domain", sizeof(r->detail));
            return 0;
        }
    }

    char cmd[512];
    /* Shell out to openssl to get the 'notAfter' date */
    snprintf(cmd, sizeof(cmd), "echo | openssl s_client -servername %.100s -connect %.100s:443 2>/dev/null | openssl x509 -noout -enddate 2>/dev/null", cfg->extra_val, cfg->extra_val);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to execute openssl", sizeof(r->detail));
        return 0;
    }

    char out[256] = {0};
    if (fgets(out, sizeof(out)-1, fp) != NULL) {
        out[strcspn(out, "\r\n")] = 0;
    }
    pclose(fp);

    /* Output format: notAfter=May 27 12:00:00 2026 GMT */
    if (strncmp(out, "notAfter=", 9) == 0) {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        
        if (strptime(out + 9, "%b %d %H:%M:%S %Y %Z", &tm) != NULL ||
            strptime(out + 9, "%b %d %H:%M:%S %Y", &tm) != NULL) {
            
            /* Assume GMT for certificates */
            putenv((char*)"TZ=GMT");
            tzset();
            time_t cert_time = mktime(&tm);
            
            time_t now = time(NULL);
            double diff_days = difftime(cert_time, now) / (60.0 * 60.0 * 24.0);
            
            if (diff_days > 0) {
                r->ok = 1; 
                r->value = diff_days;
                safe_copy(r->unit, "days", sizeof(r->unit));
                snprintf(r->detail, sizeof(r->detail), "Valid for %.1f more days", diff_days);
            } else {
                r->ok = 0; 
                r->value = diff_days;
                safe_copy(r->unit, "days", sizeof(r->unit));
                snprintf(r->detail, sizeof(r->detail), "Expired %.1f days ago", -diff_days);
            }
        } else {
            r->ok = 0; r->value = 0;
            snprintf(r->detail, sizeof(r->detail), "Failed to parse date: %.100s", out + 9);
        }
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Could not retrieve SSL cert date (domain unreachable/invalid)", sizeof(r->detail));
    }

    return 0;
}

const test_module_t mod_ssl_cert = { .type_name = "ssl_cert", .collect = ssl_cert_collect };

/* =========================================================================
 * TEST: lb_backend_state (HAProxy)
 * Queries the HAProxy UNIX socket for UP/DOWN stats of a specific backend pool.
 * Uses extra_key: "backend_pool", extra_val: "nextcloud_cluster"
 * ========================================================================= */
static int lb_backend_state_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    if (strcmp(cfg->extra_key, "backend_pool") != 0 || strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing 'backend_pool' in extra_key/val", sizeof(r->detail));
        return 0;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to create AF_UNIX socket", sizeof(r->detail));
        return 0;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/haproxy/admin.sock", sizeof(addr.sun_path)-1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot connect to HAProxy socket /run/haproxy/admin.sock", sizeof(r->detail));
        close(sock);
        return 0;
    }

    const char *cmd = "show stat\n";
    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to send command to HAProxy socket", sizeof(r->detail));
        close(sock);
        return 0;
    }

    char buffer[4096];
    int up_count = 0;
    int down_count = 0;
    
    FILE *fp = fdopen(sock, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            char *line = buffer;
            
            char *pxname = line;
            char *comma1 = strchr(pxname, ',');
            if (!comma1) continue;
            *comma1 = '\0';
            
            char *svname = comma1 + 1;
            char *comma2 = strchr(svname, ',');
            if (!comma2) continue;
            *comma2 = '\0';
            
            if (strcmp(pxname, cfg->extra_val) == 0 && strcmp(svname, "BACKEND") != 0 && strcmp(svname, "FRONTEND") != 0) {
                char *curr = comma2 + 1;
                /* Skip 15 columns to reach 'status' (index 17) */
                for (int i = 0; i < 15; i++) {
                    char *next = strchr(curr, ',');
                    if (!next) break;
                    curr = next + 1;
                }
                
                char *status = curr;
                char *end_status = strchr(status, ',');
                if (end_status) *end_status = '\0';
                
                if (strncmp(status, "UP", 2) == 0) up_count++;
                else if (strncmp(status, "DOWN", 4) == 0) down_count++;
            }
        }
        fclose(fp);
    } else {
        close(sock);
    }

    int total = up_count + down_count;
    if (total == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->unit, "nodes", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Backend pool '%.100s' not found or empty", cfg->extra_val);
    } else {
        r->ok = (up_count > 0) ? 1 : 0; 
        r->value = (double)up_count / total;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%d/%d nodes UP in pool %.100s", up_count, total, cfg->extra_val);
    }

    return 0;
}

const test_module_t mod_lb_backend = { .type_name = "lb_backend_state", .collect = lb_backend_state_collect };

void register_lb_tests(void) {
    framework_register(&mod_ssl_cert);
    framework_register(&mod_lb_backend);
}
