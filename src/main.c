#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "framework.h"
#include "output_json.h"

/* Extern declarations for all module registries */
extern void register_net_tests(void);
extern void register_services_tests(void);
extern void register_lb_tests(void);
extern void register_db_tests(void);
extern void register_redis_tests(void);
extern void register_web_tests(void);
extern void register_nfs_tests(void);
extern void register_nc_tests(void);
extern void register_nc_api_tests(void);
extern void register_sys_tests(void);

/* Extern for the YAML parser created in Step 1 */
extern int parse_yaml_config(const char *filepath, config_t *cfg);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path/to/reference.yaml>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *config_path = argv[1];

    /* 1. Initialize the module framework */
    framework_init();

    /* 2. Register all telemetry modules */
    register_net_tests();
    register_services_tests();
    register_lb_tests();
    register_db_tests();
    register_redis_tests();
    register_web_tests();
    register_nfs_tests();
    register_nc_tests();
    register_nc_api_tests();
    register_sys_tests();

    /* 3. Parse the YAML Configuration */
    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    if (parse_yaml_config(config_path, &cfg) != 0) {
        fprintf(stderr, "FATAL: Failed to parse configuration file: %s\n", config_path);
        return EXIT_FAILURE;
    }

    if (cfg.test_count == 0) {
        fprintf(stderr, "WARNING: No tests found in configuration.\n");
        return EXIT_SUCCESS;
    }

    /* 4. Allocate the results array 
     * We allocate space for up to 3 results per test block, as some tests
     * (like load_average or innodb_buffer) emit multiple metrics at once. */
    size_t max_results = cfg.test_count * 3;
    test_result_t *results = calloc(max_results, sizeof(test_result_t));
    if (!results) {
        fprintf(stderr, "FATAL: Memory allocation failed for results buffer.\n");
        return EXIT_FAILURE;
    }

    /* 5. Execute the tests */
    size_t total_results = 0;
    
    for (size_t i = 0; i < cfg.test_count; i++) {
        test_result_t out_buf[5]; /* Temporary buffer for the specific test's results */
        size_t out_count = 0;
        
        int ret = framework_run_test(&cfg.tests[i], out_buf, 5, &out_count);
        
        if (ret == 0 && out_count > 0) {
            /* Test module executed successfully, copy emitted results to main array */
            for (size_t j = 0; j < out_count; j++) {
                if (total_results < max_results) {
                    results[total_results++] = out_buf[j];
                }
            }
        } else {
            /* Fallback: Module missing, unregistered, or catastrophic failure */
            if (total_results < max_results) {
                results[total_results].ok = -1; /* Unknown / Failed to run */
                results[total_results].value = 0.0;
                results[total_results].timestamp = (long long)time(NULL);
                strncpy(results[total_results].type, cfg.tests[i].type, CONFIG_MAX_STR - 1);
                strncpy(results[total_results].display_name, cfg.tests[i].display_name, CONFIG_MAX_STR - 1);
                strncpy(results[total_results].detail, "Test execution failed or module type unregistered", CONFIG_MAX_STR - 1);
                
                total_results++;
            }
        }
    }

    /* 6. Serialize array into JSON */
    char *json_payload = generate_json_payload(&cfg, results, total_results);
    
    if (json_payload) {
        /* Print to stdout so you can pipe it to `jq` for local testing */
        printf("%s\n", json_payload);
        free(json_payload);
    } else {
        fprintf(stderr, "FATAL: Failed to generate JSON payload.\n");
    }

    /* 7. Cleanup */
    free(results);
    
    return EXIT_SUCCESS;
}
