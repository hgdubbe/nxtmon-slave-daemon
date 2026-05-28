#ifndef OUTPUT_HTTP_H
#define OUTPUT_HTTP_H

#include <stddef.h>
#include "config.h"

int post_json_payload(const config_t *cfg, const char *json_payload,
                      char *err_buf, size_t err_max);

#endif
