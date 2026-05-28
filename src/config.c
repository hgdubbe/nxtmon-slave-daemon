#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static void safe_copy(char *dst, const char *src, size_t max)
{
    if (max == 0) return;
    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* -----------------------------------------------------------------------
 * Parser state machine
 *
 * YAML structure (abbreviated):
 *   master:
 *     host: ...
 *     port: ...
 *   agent:
 *     display_name: ...
 *     interval_sec: ...
 *   tests:
 *     - type: ...
 *       display_name: ...
 *       hosts:
 *         - display_name: ...
 *           url: ...
 *           host: ...
 *           port: ...
 * --------------------------------------------------------------------- */

typedef enum {
    S_ROOT,
    S_MASTER,
    S_AGENT,
    S_TESTS,
    S_TEST_ENTRY,
    S_TEST_HOSTS,
    S_TEST_HOST_ENTRY
} parse_state_t;

int config_parse(const char *path, config_t *cfg)
{
    if (!path || !cfg) return -1;
    memset(cfg, 0, sizeof(*cfg));

    FILE *fh = fopen(path, "r");
    if (!fh) {
        fprintf(stderr, "config: cannot open '%s'\n", path);
        return -1;
    }

    yaml_parser_t parser;
    yaml_event_t  event;

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "config: failed to initialise libyaml\n");
        fclose(fh);
        return -1;
    }
    yaml_parser_set_input_file(&parser, fh);

    parse_state_t state       = S_ROOT;
    char          last_key[CONFIG_MAX_STR] = "";
    int           done        = 0;
    int           rc          = 0;

    /* Pointers to the current test / host being filled */
    test_entry_t *cur_test    = NULL;
    host_entry_t *cur_host    = NULL;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "config: YAML parse error at line %zu: %s\n",
                    parser.problem_mark.line + 1, parser.problem);
            rc = -1;
            break;
        }

        switch (event.type) {

        case YAML_STREAM_END_EVENT:
        case YAML_DOCUMENT_END_EVENT:
            done = 1;
            break;

        /* --- mapping / sequence transitions --- */
        case YAML_MAPPING_START_EVENT:
            if (state == S_TESTS) {
                /* Start of a new test entry object */
                if (cfg->test_count >= CONFIG_MAX_TESTS) {
                    fprintf(stderr, "config: too many tests (max %d)\n",
                            CONFIG_MAX_TESTS);
                    rc = -1; done = 1; break;
                }
                cur_test = &cfg->tests[cfg->test_count++];
                memset(cur_test, 0, sizeof(*cur_test));
                state = S_TEST_ENTRY;
            } else if (state == S_TEST_HOSTS) {
                /* Start of a new host entry object */
                if (!cur_test || cur_test->host_count >= CONFIG_MAX_HOSTS) {
                    fprintf(stderr, "config: too many hosts in test\n");
                    rc = -1; done = 1; break;
                }
                cur_host = &cur_test->hosts[cur_test->host_count++];
                memset(cur_host, 0, sizeof(*cur_host));
                state = S_TEST_HOST_ENTRY;
            }
            break;

        case YAML_MAPPING_END_EVENT:
            if (state == S_TEST_HOST_ENTRY)  state = S_TEST_HOSTS;
            else if (state == S_TEST_ENTRY)  state = S_TESTS;
            else if (state == S_MASTER)      state = S_ROOT;
            else if (state == S_AGENT)       state = S_ROOT;
            break;

        case YAML_SEQUENCE_START_EVENT:
            if (state == S_TEST_ENTRY && strcmp(last_key, "hosts") == 0)
                state = S_TEST_HOSTS;
            break;

        case YAML_SEQUENCE_END_EVENT:
            if (state == S_TEST_HOSTS)  state = S_TEST_ENTRY;
            else if (state == S_TESTS)  state = S_ROOT;
            break;

        /* --- scalar values --- */
        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;

            switch (state) {

            case S_ROOT:
                /* top-level keys navigate into sub-states */
                if      (strcmp(val, "master") == 0) state = S_MASTER;
                else if (strcmp(val, "agent")  == 0) state = S_AGENT;
                else if (strcmp(val, "tests")  == 0) state = S_TESTS;
                else safe_copy(last_key, val, CONFIG_MAX_STR);
                break;

            case S_MASTER:
                if (strcmp(last_key, "host") == 0)
                    safe_copy(cfg->master.host, val, CONFIG_MAX_STR);
                else if (strcmp(last_key, "port") == 0)
                    cfg->master.port = atoi(val);
                else if (strcmp(last_key, "token") == 0 ||
                         strcmp(last_key, "bearer_token") == 0)
                    safe_copy(cfg->master.token, val, CONFIG_MAX_STR);
                safe_copy(last_key, val, CONFIG_MAX_STR);
                break;

            case S_AGENT:
                if (strcmp(last_key, "display_name") == 0)
                    safe_copy(cfg->agent.display_name, val, CONFIG_MAX_STR);
                else if (strcmp(last_key, "interval_sec") == 0)
                    cfg->agent.interval_sec = atoi(val);
                safe_copy(last_key, val, CONFIG_MAX_STR);
                break;

            case S_TEST_ENTRY:
                if (cur_test) {
                    if (strcmp(last_key, "type") == 0)
                        safe_copy(cur_test->type, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "display_name") == 0)
                        safe_copy(cur_test->display_name, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "extra_key") == 0)
                        safe_copy(cur_test->extra_key, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "extra_val") == 0)
                        safe_copy(cur_test->extra_val, val, CONFIG_MAX_STR);
                }
                safe_copy(last_key, val, CONFIG_MAX_STR);
                break;

            case S_TEST_HOST_ENTRY:
                if (cur_host) {
                    if (strcmp(last_key, "display_name") == 0)
                        safe_copy(cur_host->display_name, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "url") == 0)
                        safe_copy(cur_host->url, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "host") == 0)
                        safe_copy(cur_host->host, val, CONFIG_MAX_STR);
                    else if (strcmp(last_key, "port") == 0)
                        cur_host->port = atoi(val);
                }
                safe_copy(last_key, val, CONFIG_MAX_STR);
                break;

            default:
                safe_copy(last_key, val, CONFIG_MAX_STR);
                break;
            }
            break;
        }

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fh);
    return rc;
}

/* -----------------------------------------------------------------------
 * config_dump — human-readable output for --dump-config
 * --------------------------------------------------------------------- */

void config_dump(const config_t *cfg)
{
    if (!cfg) return;

    printf("master:\n");
    printf("  host: %s\n", cfg->master.host);
    printf("  port: %d\n", cfg->master.port);
    printf("  token: %s\n", cfg->master.token[0] ? "(configured)" : "");
    printf("agent:\n");
    printf("  display_name: %s\n", cfg->agent.display_name);
    printf("  interval_sec: %d\n", cfg->agent.interval_sec);
    printf("tests: (%zu entries)\n", cfg->test_count);

    for (size_t i = 0; i < cfg->test_count; i++) {
        const test_entry_t *t = &cfg->tests[i];
        printf("  [%zu] type='%s' display_name='%s'\n",
               i, t->type, t->display_name);
        if (t->host_count > 0) {
            printf("       hosts: (%zu)\n", t->host_count);
            for (size_t j = 0; j < t->host_count; j++) {
                const host_entry_t *h = &t->hosts[j];
                printf("         [%zu] display_name='%s' url='%s' host='%s' port=%d\n",
                       j, h->display_name, h->url, h->host, h->port);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * config_free — kept for API stability
 * --------------------------------------------------------------------- */

void config_free(config_t *cfg)
{
    (void)cfg; /* all storage is static arrays inside config_t */
}
