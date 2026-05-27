#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mysql/mysql.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* Helper to establish a MariaDB connection using config params.
 * Expects cfg->hosts[0] for host/port. 
 * If extra_key="credentials", expects extra_val="user:password".
 * Returns an active MYSQL handle or NULL on failure.
 */
static MYSQL* db_connect_helper(const test_entry_t *cfg, char *err_buf, size_t err_max) {
    MYSQL *conn = mysql_init(NULL);
    if (!conn) {
        snprintf(err_buf, err_max, "mysql_init failed");
        return NULL;
    }

    const char *host = "127.0.0.1";
    int port = 3306;
    if (cfg->host_count > 0) {
        if (strlen(cfg->hosts[0].host) > 0) host = cfg->hosts[0].host;
        if (cfg->hosts[0].port > 0) port = cfg->hosts[0].port;
    }

    char user[64] = "nxtmon";
    char pass[64] = "";
    
    if (strcmp(cfg->extra_key, "credentials") == 0) {
        char creds[128];
        safe_copy(creds, cfg->extra_val, sizeof(creds));
        char *colon = strchr(creds, ':');
        if (colon) {
            *colon = '\0';
            safe_copy(user, creds, sizeof(user));
            safe_copy(pass, colon + 1, sizeof(pass));
        } else {
            safe_copy(user, creds, sizeof(user));
        }
    }

    if (!mysql_real_connect(conn, host, user, pass[0] ? pass : NULL, NULL, port, NULL, 0)) {
        snprintf(err_buf, err_max, "Connect error: %.100s", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }
    return conn;
}

/* =========================================================================
 * TEST: db_connections
 * Queries Threads_connected and max_connections to return usage fraction.
 * ========================================================================= */
static int db_connections_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    int threads_connected = -1;
    int max_connections = -1;

    if (mysql_query(conn, "SHOW GLOBAL STATUS LIKE 'Threads_connected'") == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[1]) threads_connected = atoi(row[1]);
            mysql_free_result(res);
        }
    }

    if (mysql_query(conn, "SHOW GLOBAL VARIABLES LIKE 'max_connections'") == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[1]) max_connections = atoi(row[1]);
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (threads_connected >= 0 && max_connections > 0) {
        r->ok = 1;
        r->value = (double)threads_connected / max_connections;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%d / %d connections used", threads_connected, max_connections);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to query connection metrics", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_connections = { .type_name = "db_connections", .collect = db_connections_collect };

/* =========================================================================
 * TEST: db_rw_ro
 * Checks if the MariaDB instance is currently in read_only mode.
 * ========================================================================= */
static int db_rw_ro_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    int read_only = -1;
    if (mysql_query(conn, "SELECT @@global.read_only") == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) read_only = atoi(row[0]);
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (read_only >= 0) {
        r->ok = 1;
        r->value = (double)read_only;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Database is %s", read_only ? "Read-Only (RO)" : "Read-Write (RW)");
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to query read_only variable", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_rw_ro = { .type_name = "db_rw_ro", .collect = db_rw_ro_collect };

/* =========================================================================
 * TEST: db_gtid
 * Retrieves the current GTID position (MariaDB specific).
 * ========================================================================= */
static int db_gtid_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    char gtid[128] = {0};
    if (mysql_query(conn, "SELECT @@global.gtid_current_pos") == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) safe_copy(gtid, row[0], sizeof(gtid));
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (gtid[0]) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "string", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "GTID: %.100s", gtid);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to query @@global.gtid_current_pos", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_gtid = { .type_name = "db_gtid", .collect = db_gtid_collect };

/* =========================================================================
 * REGISTRY EXPORT
 * ========================================================================= */
void register_db_tests(void) {
    framework_register(&mod_db_connections);
    framework_register(&mod_db_rw_ro);
    framework_register(&mod_db_gtid);
}
