#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <stddef.h>
#include "config.h"

#define MAX_RESULTS_PER_TEST 16

/* Standardized output format defined in readme.md */
typedef struct {
    char type[CONFIG_MAX_STR];
    char display_name[CONFIG_MAX_STR];
    int ok;               /* 1=success, 0=fail, -1=unknown */
    double value;
    char unit[64];        /* e.g., "fraction", "ms", "http_status" */
    char detail[256];     /* human-readable context/errors */
    long long timestamp;  /* epoch timestamp */
} test_result_t;

/* Vtable function pointers for a test module */
typedef int  (*test_init_fn)(void);
typedef int  (*test_collect_fn)(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count);
typedef void (*test_shutdown_fn)(void);

/* The Module Registry structure */
typedef struct {
    const char *type_name;    /* Matches config 'type' (e.g. "cpu") */
    test_init_fn init;        /* Optional */
    test_collect_fn collect;  /* Mandatory */
    test_shutdown_fn shutdown;/* Optional */
} test_module_t;

/* Framework API */
void framework_init(void);
int  framework_register(const test_module_t *mod);
int  framework_run_test(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count);
void framework_shutdown(void);

#endif
