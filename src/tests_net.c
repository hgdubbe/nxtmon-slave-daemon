#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    snprintf(dst, max, "%s", src);
}

/* =========================================================================
 * TEST: tcp_connect
 * ========================================================================= */
static int net_tcp_connect_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (cfg->host_count == 0 || strlen(cfg->hosts[0].host) == 0 || cfg->hosts[0].port <= 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Invalid host configuration (missing host or port)", sizeof(r->detail));
        return 0;
    }

    struct hostent *he = gethostbyname(cfg->hosts[0].host);
    if (!he) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "DNS resolution failed for %.100s", cfg->hosts[0].host);
        return 0;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Socket creation failed", sizeof(r->detail));
        return 0;
    }

    /* Set a 3-second timeout */
    struct timeval tv;
    tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(cfg->hosts[0].port);
    server.sin_addr = *((struct in_addr *)he->h_addr_list[0]);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int res = connect(sock, (struct sockaddr *)&server, sizeof(server));
    clock_gettime(CLOCK_MONOTONIC, &end);

    close(sock);

    if (res == 0) {
        double ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
        r->ok = 1;
        r->value = ms;
        safe_copy(r->unit, "ms", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Connected in %.2f ms", ms);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Connection refused or timed out", sizeof(r->detail));
    }
    return 0;
}

const test_module_t mod_net_tcp_connect = { .type_name = "tcp_connect", .collect = net_tcp_connect_collect };

void register_net_tests(void) {
    framework_register(&mod_net_tcp_connect);
}
