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
    if (max == 0) return;
    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* =========================================================================
 * TEST: ssl_cert
 * Checks SSL certificate expiration days remaining for a given domain.
 * Uses extra_key: "domain", extra_val: "example.com" or hosts[0].host.
 * Requires `openssl` binary in PATH.
 * ========================================================================= */
static int ssl_cert_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    const char *domain = NULL;
    int port = 443;
    if (strcmp(cfg->extra_key, "domain") == 0 && strlen(cfg->extra_val) > 0) {
        domain = cfg->extra_val;
    } else if (cfg->host_count > 0 && strlen(cfg->hosts[0].host) > 0) {
        domain = cfg->hosts[0].host;
        if (cfg->hosts[0].port > 0) port = cfg->hosts[0].port;
    } else if (cfg->extra_key[0] == '\0' && strlen(cfg->extra_val) > 0) {
        domain = cfg->extra_val;
    }

    if (!domain) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing domain in extra_val or hosts[0].host", sizeof(r->detail));
        return 0;
    }

    /* Basic sanitization (prevent shell injection) */
    for (size_t i = 0; i < strlen(domain); i++) {
        if (!isalnum(domain[i]) && domain[i] != '-' && domain[i] != '.') {
            r->ok = 0; r->value = 0;
            safe_copy(r->detail, "Invalid characters in domain", sizeof(r->detail));
            return 0;
        }
    }

    char cmd[512];
    /* Shell out to openssl to get the 'notAfter' date */
    snprintf(cmd, sizeof(cmd), "echo | openssl s_client -servername %.100s -connect %.100s:%d 2>/dev/null | openssl x509 -noout -enddate 2>/dev/null", domain, domain, port);
    
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
 * Queries the HAProxy UNIX socket for UP/DOWN backend/server stats.
 * extra_key "socket" treats extra_val as the socket path and aggregates all pools.
 * extra_key "backend_pool" filters by pool and uses the default HAProxy socket path.
 * ========================================================================= */
static int lb_backend_state_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();
    
    char socket_path[CONFIG_MAX_STR];
    char backend_pool[CONFIG_MAX_STR];
    safe_copy(socket_path, "/run/haproxy/admin.sock", sizeof(socket_path));
    backend_pool[0] = '\0';

    if (strcmp(cfg->extra_key, "backend_pool") == 0 && strlen(cfg->extra_val) > 0) {
        safe_copy(backend_pool, cfg->extra_val, sizeof(backend_pool));
    } else if ((strcmp(cfg->extra_key, "socket") == 0 || cfg->extra_key[0] == '\0') &&
               strlen(cfg->extra_val) > 0) {
        safe_copy(socket_path, cfg->extra_val, sizeof(socket_path));
    } else if (strlen(cfg->extra_val) == 0) {
        /* Keep the default socket path and aggregate all backend servers. */
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Invalid extra_key for lb_backend_state", sizeof(r->detail));
        return 0;
    }

    if (strlen(socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "HAProxy socket path too long", sizeof(r->detail));
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
    memcpy(addr.sun_path, socket_path, strlen(socket_path) + 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Cannot connect to HAProxy socket %.120s", socket_path);
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
            
            if ((backend_pool[0] == '\0' || strcmp(pxname, backend_pool) == 0) &&
                strcmp(svname, "BACKEND") != 0 &&
                strcmp(svname, "FRONTEND") != 0) {
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
        if (backend_pool[0]) {
            snprintf(r->detail, sizeof(r->detail), "Backend pool '%.100s' not found or empty", backend_pool);
        } else {
            snprintf(r->detail, sizeof(r->detail), "No backend server rows found via %.120s", socket_path);
        }
    } else {
        r->ok = (up_count > 0) ? 1 : 0; 
        r->value = (double)up_count / total;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        if (backend_pool[0]) {
            snprintf(r->detail, sizeof(r->detail), "%d/%d nodes UP in pool %.100s", up_count, total, backend_pool);
        } else {
            snprintf(r->detail, sizeof(r->detail), "%d/%d HAProxy backend nodes UP", up_count, total);
        }
    }

    return 0;
}

const test_module_t mod_lb_backend = { .type_name = "lb_backend_state", .collect = lb_backend_state_collect };

void register_lb_tests(void) {
    framework_register(&mod_ssl_cert);
    framework_register(&mod_lb_backend);
}
