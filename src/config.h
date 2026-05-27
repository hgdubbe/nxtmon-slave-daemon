#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/* Maximum string field sizes */
#define CONFIG_MAX_STR     256
#define CONFIG_MAX_HOSTS   32
#define CONFIG_MAX_TESTS   128

/* A single host entry inside a test (e.g. url_reachable hosts list) */
typedef struct {
    char display_name[CONFIG_MAX_STR];
    char url[CONFIG_MAX_STR];          /* used by url_reachable */
    char host[CONFIG_MAX_STR];         /* used by tcp_connect / ping */
    int  port;                         /* used by tcp_connect */
} host_entry_t;

/* A single test entry from the tests[] YAML array */
typedef struct {
    char         type[CONFIG_MAX_STR];
    char         display_name[CONFIG_MAX_STR];
    host_entry_t hosts[CONFIG_MAX_HOSTS];
    size_t       host_count;
    /* generic key=value extra args for future test types */
    char         extra_key[CONFIG_MAX_STR];
    char         extra_val[CONFIG_MAX_STR];
} test_entry_t;

/* master: block */
typedef struct {
    char host[CONFIG_MAX_STR];
    int  port;
} master_cfg_t;

/* agent: block */
typedef struct {
    char display_name[CONFIG_MAX_STR];
    int  interval_sec;
} agent_cfg_t;

/* Top-level config */
typedef struct {
    master_cfg_t  master;
    agent_cfg_t   agent;
    test_entry_t  tests[CONFIG_MAX_TESTS];
    size_t        test_count;
} config_t;

/**
 * Parse YAML file at `path` into *cfg.
 * Returns 0 on success, -1 on error (message printed to stderr).
 */
int  config_parse(const char *path, config_t *cfg);

/**
 * Print the effective config to stdout (for --dump-config).
 */
void config_dump(const config_t *cfg);

/**
 * Free any dynamically allocated resources held by cfg.
 * (Currently a no-op since all storage is static arrays, kept for API stability.)
 */
void config_free(config_t *cfg);

#endif /* CONFIG_H */
