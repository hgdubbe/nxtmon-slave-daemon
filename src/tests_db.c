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

/* =========================================================================
 * CONNECTION HELPER
 * ========================================================================= */
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
 * TEST: db_latency_read
 * Queries performance_schema for average SELECT latency in ms.
 * ========================================================================= */
static int db_latency_read_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    double latency_ms = -1.0;
    const char *query = "SELECT SUM_TIMER_WAIT / COUNT_STAR / 1000000000.0 FROM performance_schema.events_statements_summary_global_by_event_name WHERE EVENT_NAME = 'statement/sql/select' AND COUNT_STAR > 0";
    
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) latency_ms = atof(row[0]);
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (latency_ms >= 0) {
        r->ok = 1; r->value = latency_ms;
        safe_copy(r->unit, "ms", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Read latency: %.3f ms", latency_ms);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to query performance_schema (is it enabled?)", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_latency_read = { .type_name = "db_latency_read", .collect = db_latency_read_collect };

/* =========================================================================
 * TEST: db_latency_write
 * Queries performance_schema for average INSERT/UPDATE/DELETE latency in ms.
 * ========================================================================= */
static int db_latency_write_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    double latency_ms = -1.0;
    const char *query = "SELECT SUM(SUM_TIMER_WAIT) / SUM(COUNT_STAR) / 1000000000.0 FROM performance_schema.events_statements_summary_global_by_event_name WHERE EVENT_NAME IN ('statement/sql/insert', 'statement/sql/update', 'statement/sql/delete') AND COUNT_STAR > 0";
    
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) latency_ms = atof(row[0]);
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (latency_ms >= 0) {
        r->ok = 1; r->value = latency_ms;
        safe_copy(r->unit, "ms", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Write latency: %.3f ms", latency_ms);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to query performance_schema for writes", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_latency_write = { .type_name = "db_latency_write", .collect = db_latency_write_collect };

/* =========================================================================
 * TEST: db_master_slave
 * Checks if the node is operating as a Replication Master or Slave.
 * ========================================================================= */
static int db_master_slave_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    int is_slave = 0;
    int success = 0;
    if (mysql_query(conn, "SHOW SLAVE STATUS") == 0) {
        success = 1;
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            if (mysql_fetch_row(res)) is_slave = 1;
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (success) {
        r->ok = 1;
        r->value = is_slave ? 0.0 : 1.0; /* 1.0 = Master, 0.0 = Slave */
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Node is %s", is_slave ? "Slave" : "Master");
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to execute SHOW SLAVE STATUS", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_master_slave = { .type_name = "db_master_slave", .collect = db_master_slave_collect };

/* =========================================================================
 * TEST: db_innodb_buffer
 * Returns 2 metrics: Buffer Pool Usage Fraction & Buffer Pool Hit Rate.
 * ========================================================================= */
static int db_innodb_buffer_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 2) return -1; /* Needs 2 slots */
    
    char err[256];
    MYSQL *conn = db_connect_helper(cfg, err, sizeof(err));
    if (!conn) {
        *out_count = 1;
        test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = get_timestamp();
        r->ok = 0; r->value = 0; safe_copy(r->detail, err, sizeof(r->detail));
        return 0;
    }

    unsigned long long reads = 0, requests = 0, pages_data = 0, pages_total = 0;
    int success = 0;
    
    if (mysql_query(conn, "SHOW GLOBAL STATUS WHERE Variable_name IN ('Innodb_buffer_pool_reads', 'Innodb_buffer_pool_read_requests', 'Innodb_buffer_pool_pages_data', 'Innodb_buffer_pool_pages_total')") == 0) {
        success = 1;
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                if (!row[0] || !row[1]) continue;
                if (strcmp(row[0], "Innodb_buffer_pool_reads") == 0) reads = strtoull(row[1], NULL, 10);
                else if (strcmp(row[0], "Innodb_buffer_pool_read_requests") == 0) requests = strtoull(row[1], NULL, 10);
                else if (strcmp(row[0], "Innodb_buffer_pool_pages_data") == 0) pages_data = strtoull(row[1], NULL, 10);
                else if (strcmp(row[0], "Innodb_buffer_pool_pages_total") == 0) pages_total = strtoull(row[1], NULL, 10);
            }
            mysql_free_result(res);
        }
    }
    mysql_close(conn);

    if (success && pages_total > 0 && requests > 0) {
        long long ts = get_timestamp();
        
        /* Metric 1: Usage */
        test_result_t *r1 = &results[0];
        safe_copy(r1->type, cfg->type, CONFIG_MAX_STR);
        snprintf(r1->display_name, CONFIG_MAX_STR, "%.100s (Usage)", cfg->display_name);
        r1->timestamp = ts;
        r1->ok = 1;
        r1->value = (double)pages_data / pages_total;
        safe_copy(r1->unit, "fraction", sizeof(r1->unit));
        snprintf(r1->detail, sizeof(r1->detail), "Used: %llu / %llu pages", pages_data, pages_total);
        
        /* Metric 2: Hit Rate */
        test_result_t *r2 = &results[1];
        safe_copy(r2->type, cfg->type, CONFIG_MAX_STR);
        snprintf(r2->display_name, CONFIG_MAX_STR, "%.100s (Hit Rate)", cfg->display_name);
        r2->timestamp = ts;
        r2->ok = 1;
        r2->value = 1.0 - ((double)reads / requests);
        safe_copy(r2->unit, "fraction", sizeof(r2->unit));
        snprintf(r2->detail, sizeof(r2->detail), "Hit rate based on %llu reads vs %llu requests", reads, requests);
        
        *out_count = 2;
    } else {
        *out_count = 1;
        test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = get_timestamp();
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to calculate InnoDB metrics", sizeof(r->detail));
    }
    return 0;
}
const test_module_t mod_db_innodb_buffer = { .type_name = "db_innodb_buffer", .collect = db_innodb_buffer_collect };

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
 * Note: Framework inherently adds a timestamp to all collected test results.
 * This directly supports the sync-status goal (timestamped GTID comparison).
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
    framework_register(&mod_db_latency_read);
    framework_register(&mod_db_latency_write);
    framework_register(&mod_db_master_slave);
    framework_register(&mod_db_innodb_buffer);
    framework_register(&mod_db_connections);
    framework_register(&mod_db_rw_ro);
    framework_register(&mod_db_gtid);
}
