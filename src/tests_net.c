#include "framework.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

/* Helper for standard epoch timestamps */
static long long get_timestamp(void) {
    return (long long)time(NULL);
}

/* Helper for safe string truncation */
static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* =========================================================================
 * TEST: hostname
 * Retrieves the local system hostname.
 * ========================================================================= */
static int net_hostname_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    
    char host[256] = {0};
    int ok = (gethostname(host, sizeof(host)) == 0) ? 1 : 0;
    
    test_result_t *r = &results[0];
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->ok = ok;
    r->value = ok ? 1.0 : 0.0;
    safe_copy(r->unit, "status", sizeof(r->unit));
    
    if (ok) {
        safe_copy(r->detail, host, sizeof(r->detail));
    } else {
        safe_copy(r->detail, "Failed to read hostname", sizeof(r->detail));
    }
    r->timestamp = get_timestamp();
    
    *out_count = 1;
    return 0;
}

const test_module_t mod_hostname = {
    .type_name = "hostname",
    .init = NULL,
    .collect = net_hostname_collect,
    .shutdown = NULL
};

/* =========================================================================
 * TEST: tcp_connect
 * Performs a non-blocking TCP connection test to verify port reachability.
 * Uses the config 'hosts' array (host + port).
 * ========================================================================= */
static int net_tcp_connect_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    *out_count = 0;
    
    for (size_t i = 0; i < cfg->host_count && *out_count < max_results; i++) {
        const host_entry_t *h = &cfg->hosts[i];
        test_result_t *r = &results[*out_count];
        
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        
        /* Combine test display name with host display name securely */
        snprintf(r->display_name, CONFIG_MAX_STR, "%.100s (%.100s)", cfg->display_name, h->display_name);
        r->timestamp = get_timestamp();
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            r->ok = 0;
            r->value = 0.0;
            safe_copy(r->detail, "Failed to create socket", sizeof(r->detail));
        } else {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(h->port);
            
            if (inet_pton(AF_INET, h->host, &addr.sin_addr) <= 0) {
                r->ok = 0;
                r->value = 0.0;
                safe_copy(r->detail, "Invalid IP address", sizeof(r->detail));
                close(sock);
                (*out_count)++;
                continue;
            }
            
            /* Set socket to non-blocking to enforce a timeout */
            int flags = fcntl(sock, F_GETFL, 0);
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);
            
            int res = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
            if (res < 0 && errno == EINPROGRESS) {
                /* Wait for connection to complete or timeout (e.g., 2 seconds) */
                struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(sock, &fdset);
                
                res = select(sock + 1, NULL, &fdset, NULL, &tv);
                if (res == 1) {
                    /* Socket is writable, check for socket errors */
                    int so_error;
                    socklen_t len = sizeof(so_error);
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                    if (so_error == 0) res = 0; /* Success */
                    else res = -1; /* Failed */
                } else {
                    res = -1; /* Timeout or error */
                }
            }
            
            if (res == 0) {
                r->ok = 1;
                r->value = 1.0;
                safe_copy(r->unit, "boolean", sizeof(r->unit));
                snprintf(r->detail, sizeof(r->detail), "Connected to %.100s:%d", h->host, h->port);
            } else {
                r->ok = 0;
                r->value = 0.0;
                safe_copy(r->unit, "boolean", sizeof(r->unit));
                snprintf(r->detail, sizeof(r->detail), "Connection timeout/refused to %.100s:%d", h->host, h->port);
            }
            close(sock);
        }
        (*out_count)++;
    }
    return 0;
}

const test_module_t mod_tcp_connect = {
    .type_name = "tcp_connect",
    .init = NULL,
    .collect = net_tcp_connect_collect,
    .shutdown = NULL
};

/* =========================================================================
 * REGISTRY EXPORT
 * ========================================================================= */
void register_network_tests(void) {
    framework_register(&mod_hostname);
    framework_register(&mod_tcp_connect);
}
