#include "framework.h"
#include <string.h>

#define MAX_MODULES 128

static const test_module_t *modules[MAX_MODULES];
static size_t num_modules = 0;

void framework_init(void) {
    num_modules = 0;
}

int framework_register(const test_module_t *mod) {
    if (!mod || !mod->type_name || !mod->collect) return -1;
    if (num_modules >= MAX_MODULES) return -1;
    
    if (mod->init) {
        if (mod->init() != 0) return -1;
    }
    
    modules[num_modules++] = mod;
    return 0;
}

int framework_run_test(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (!cfg || !results || !out_count) return -1;
    *out_count = 0;
    
    for (size_t i = 0; i < num_modules; i++) {
        if (strcmp(modules[i]->type_name, cfg->type) == 0) {
            return modules[i]->collect(cfg, results, max_results, out_count);
        }
    }
    
    /* Module not found for this type */
    return -1;
}

void framework_shutdown(void) {
    for (size_t i = 0; i < num_modules; i++) {
        if (modules[i]->shutdown) {
            modules[i]->shutdown();
        }
    }
    num_modules = 0;
}
