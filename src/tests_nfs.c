#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/time.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    if (max == 0) return;
    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + (tv.tv_usec / 1000000.0);
}

/* =========================================================================
 * TEST: nfs_exports
 * Checks if /var/lib/nfs/etab contains the exported directory.
 * If no extra_val is provided, checks if etab is simply non-empty.
 * ========================================================================= */
static int nfs_exports_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    FILE *fp = fopen("/var/lib/nfs/etab", "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot open /var/lib/nfs/etab (is NFS server installed?)", sizeof(r->detail));
        return 0;
    }

    char line[512];
    int found = 0;
    int has_target = (strlen(cfg->extra_val) > 0);

    while (fgets(line, sizeof(line), fp)) {
        if (!has_target) {
            found = 1; break; /* Any export is fine */
        } else {
            /* Basic check if the line starts with the export path */
            if (strncmp(line, cfg->extra_val, strlen(cfg->extra_val)) == 0) {
                found = 1; break;
            }
        }
    }
    fclose(fp);

    if (found) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        if (has_target) snprintf(r->detail, sizeof(r->detail), "Export found: %.100s", cfg->extra_val);
        else safe_copy(r->detail, "NFS exports are active", sizeof(r->detail));
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        if (has_target) snprintf(r->detail, sizeof(r->detail), "Export not found: %.100s", cfg->extra_val);
        else safe_copy(r->detail, "No active NFS exports found", sizeof(r->detail));
    }

    return 0;
}

/* =========================================================================
 * TEST: nfs_mounts
 * Checks /proc/mounts to see if the specified mountpoint is an nfs mount.
 * Uses extra_val for mountpoint (e.g., /mnt/nfs_share).
 * ========================================================================= */
static int nfs_mounts_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing mountpoint in extra_val", sizeof(r->detail));
        return 0;
    }

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot open /proc/mounts", sizeof(r->detail));
        return 0;
    }

    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], fs[128];
        if (sscanf(line, "%255s %255s %127s", dev, mnt, fs) == 3) {
            if (strcmp(mnt, cfg->extra_val) == 0) {
                if (strcmp(fs, "nfs") == 0 || strcmp(fs, "nfs4") == 0) {
                    found = 1; break;
                }
            }
        }
    }
    fclose(fp);

    if (found) {
        r->ok = 1; r->value = 1.0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "NFS mount active on %.100s", cfg->extra_val);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->unit, "boolean", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Mountpoint %.100s is not an active NFS mount", cfg->extra_val);
    }
    return 0;
}

/* =========================================================================
 * TEST: nfs_rw_test
 * Performs a literal write-and-delete on the mountpoint to measure latency.
 * ========================================================================= */
static int nfs_rw_test_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing mountpoint in extra_val", sizeof(r->detail));
        return 0;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%.200s/.nxtmon_rw_test_%lld", cfg->extra_val, r->timestamp);

    double start = get_time_sec();
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Failed to open test file for writing: %.100s", filepath);
        return 0;
    }
    fprintf(fp, "nxtmon_test\n");
    fclose(fp);

    if (remove(filepath) != 0) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "Failed to delete test file: %.100s", filepath);
        return 0;
    }

    double end = get_time_sec();
    double elapsed_ms = (end - start) * 1000.0;

    r->ok = 1;
    r->value = elapsed_ms;
    safe_copy(r->unit, "ms", sizeof(r->unit));
    snprintf(r->detail, sizeof(r->detail), "RW test passed in %.2f ms", elapsed_ms);

    return 0;
}

/* =========================================================================
 * TEST: nfs_iowait
 * Reads /proc/stat to calculate system iowait percentage over a 100ms sample.
 * ========================================================================= */
static int get_cpu_ticks(unsigned long long *total, unsigned long long *iowait) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            unsigned long long user, nice, system, idle, io, irq, softirq, steal;
            if (sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                &user, &nice, &system, &idle, &io, &irq, &softirq, &steal) == 8) {
                *total = user + nice + system + idle + io + irq + softirq + steal;
                *iowait = io;
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int nfs_iowait_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    unsigned long long t1, i1, t2, i2;
    if (!get_cpu_ticks(&t1, &i1)) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to read /proc/stat", sizeof(r->detail)); return 0;
    }

    usleep(100000); /* 100ms sample window */

    if (!get_cpu_ticks(&t2, &i2)) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to read /proc/stat (2nd pass)", sizeof(r->detail)); return 0;
    }

    unsigned long long total_diff = t2 - t1;
    unsigned long long iowait_diff = i2 - i1;

    if (total_diff > 0) {
        r->ok = 1;
        r->value = (double)iowait_diff / total_diff;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "IO Wait is %.2f%%", r->value * 100.0);
    } else {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Tick delta zero", sizeof(r->detail));
    }
    return 0;
}

/* =========================================================================
 * TEST: nfs_inode
 * Checks inode usage fraction on the NFS export/mount using statvfs.
 * Uses extra_val for path.
 * ========================================================================= */
static int nfs_inode_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    if (strlen(cfg->extra_val) == 0) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Missing directory path in extra_val", sizeof(r->detail));
        return 0;
    }

    struct statvfs st;
    if (statvfs(cfg->extra_val, &st) != 0) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "statvfs failed on %.100s", cfg->extra_val);
        return 0;
    }

    if (st.f_files > 0) {
        r->ok = 1;
        unsigned long long used_inodes = st.f_files - st.f_ffree;
        r->value = (double)used_inodes / st.f_files;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "%llu / %llu inodes used", used_inodes, (unsigned long long)st.f_files);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Filesystem reports 0 total inodes (not supported?)", sizeof(r->detail));
    }

    return 0;
}

const test_module_t mod_nfs_exports = { .type_name = "nfs_exports", .collect = nfs_exports_collect };
const test_module_t mod_nfs_mounts  = { .type_name = "nfs_mounts",  .collect = nfs_mounts_collect };
const test_module_t mod_nfs_rw_test = { .type_name = "nfs_rw_test", .collect = nfs_rw_test_collect };
const test_module_t mod_nfs_iowait  = { .type_name = "nfs_iowait",  .collect = nfs_iowait_collect };
const test_module_t mod_nfs_inode   = { .type_name = "nfs_inode",   .collect = nfs_inode_collect };

void register_nfs_tests(void) {
    framework_register(&mod_nfs_exports);
    framework_register(&mod_nfs_mounts);
    framework_register(&mod_nfs_rw_test);
    framework_register(&mod_nfs_iowait);
    framework_register(&mod_nfs_inode);
}
