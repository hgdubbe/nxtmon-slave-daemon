#ifndef OUTPUT_JSON_H
#define OUTPUT_JSON_H

#include "framework.h"
#include "config.h"

/* Takes the executed test results and serializes them into the master JSON schema */
char* generate_json_payload(const config_t *cfg, const test_result_t *results, size_t count);

#endif
