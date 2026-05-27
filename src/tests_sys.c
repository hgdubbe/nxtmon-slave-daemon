#include "framework.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/statvfs.h>

static long long get_timestamp(void) {
    return (long long)time(NULL);
}

static void safe_copy(char *dst, const char *src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* =========================================================================
 * TEST: cpu
 * Calculates CPU usage percentage by comparing /proc/stat ticks.
 * Takes a 100ms sample window.
 * ========================================================================= */
static int get_cpu_ticks(unsigned long long *idle_out, unsigned long long *total_out) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
                
                *idle_out = idle + iowait;
                *total_out = user + nice + system + idle + iowait + irq + softirq + steal;
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int sys_cpu_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    unsigned long long idle1, total1, idle2, total2;
    
    if (!get_cpu_ticks(&idle1, &total1)) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to read /proc/stat", sizeof(r->detail));
        return 0;
    }

    usleep(100000); /* 100ms delay */

    if (!get_cpu_ticks(&idle2, &total2)) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to read /proc/stat (2nd pass)", sizeof(r->detail));
        return 0;
    }

    unsigned long long total_diff = total2 - total1;
    unsigned long long idle_diff = idle2 - idle1;

    if (total_diff > 0) {
        double cpu_fraction = 1.0 - ((double)idle_diff / total_diff);
        r->ok = 1;
        r->value = cpu_fraction;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "CPU Load: %.1f%%", cpu_fraction * 100.0);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "CPU tick delta zero", sizeof(r->detail));
    }

    return 0;
}

/* =========================================================================
 * TEST: ram
 * Parses /proc/meminfo to calculate RAM usage fraction.
 * ========================================================================= */
static int sys_ram_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot open /proc/meminfo", sizeof(r->detail));
        return 0;
    }

    unsigned long long mem_total = 0, mem_avail = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %llu kB", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %llu kB", &mem_avail);
        }
    }
    fclose(fp);

    if (mem_total > 0 && mem_avail > 0) {
        unsigned long long mem_used = mem_total - mem_avail;
        double ram_frac = (double)mem_used / mem_total;
        
        r->ok = 1;
        r->value = ram_frac;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "RAM: %.1f GB used / %.1f GB total", 
                 (double)mem_used / 1024 / 1024, (double)mem_total / 1024 / 1024);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to parse MemTotal or MemAvailable", sizeof(r->detail));
    }

    return 0;
}

/* =========================================================================
 * TEST: load_average
 * Extracts the 1m, 5m, and 15m load averages from /proc/loadavg
 * ========================================================================= */
static int sys_load_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    *out_count = 0;

    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = get_timestamp();
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Cannot open /proc/loadavg", sizeof(r->detail));
        *out_count = 1;
        return 0;
    }

    double load1, load5, load15;
    if (fscanf(fp, "%lf %lf %lf", &load1, &load5, &load15) == 3) {
        long long ts = get_timestamp();
        
        if (*out_count < max_results) {
            test_result_t *r = &results[(*out_count)++];
            safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
            snprintf(r->display_name, CONFIG_MAX_STR, "%.100s (1m)", cfg->display_name);
            r->timestamp = ts; r->ok = 1; r->value = load1;
            safe_copy(r->unit, "load", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "1-minute load average");
        }
        if (*out_count < max_results) {
            test_result_t *r = &results[(*out_count)++];
            safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
            snprintf(r->display_name, CONFIG_MAX_STR, "%.100s (5m)", cfg->display_name);
            r->timestamp = ts; r->ok = 1; r->value = load5;
            safe_copy(r->unit, "load", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "5-minute load average");
        }
        if (*out_count < max_results) {
            test_result_t *r = &results[(*out_count)++];
            safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
            snprintf(r->display_name, CONFIG_MAX_STR, "%.100s (15m)", cfg->display_name);
            r->timestamp = ts; r->ok = 1; r->value = load15;
            safe_copy(r->unit, "load", sizeof(r->unit));
            snprintf(r->detail, sizeof(r->detail), "15-minute load average");
        }
    } else {
        test_result_t *r = &results[0];
        safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
        safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
        r->timestamp = get_timestamp();
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Failed to parse /proc/loadavg", sizeof(r->detail));
        *out_count = 1;
    }

    fclose(fp);
    return 0;
}

/* =========================================================================
 * TEST: disk_usage
 * Checks root partition usage using statvfs.
 * ========================================================================= */
static int sys_disk_usage_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    const char *path = strlen(cfg->extra_val) > 0 ? cfg->extra_val : "/";

    struct statvfs st;
    if (statvfs(path, &st) != 0) {
        r->ok = 0; r->value = 0;
        snprintf(r->detail, sizeof(r->detail), "statvfs failed on %.100s", path);
        return 0;
    }

    if (st.f_blocks > 0) {
        unsigned long long total = st.f_blocks * st.f_frsize;
        unsigned long long free  = st.f_bfree * st.f_frsize;
        unsigned long long used  = total - free;
        
        r->ok = 1;
        r->value = (double)used / total;
        safe_copy(r->unit, "fraction", sizeof(r->unit));
        snprintf(r->detail, sizeof(r->detail), "Disk: %.1f GB used / %.1f GB total (%.100s)", 
                 (double)used / 1024 / 1024 / 1024, 
                 (double)total / 1024 / 1024 / 1024, path);
    } else {
        r->ok = 0; r->value = 0;
        safe_copy(r->detail, "Filesystem reports 0 total blocks", sizeof(r->detail));
    }

    return 0;
}

/* =========================================================================
 * TEST: disk_io
 * Parses /proc/diskstats to calculate global I/O operations per second (IOPS).
 * ========================================================================= */
static int get_global_io(unsigned long long *ios_out) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return 0;
    
    unsigned long long total_ios = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), fp)) {
        int major, minor;
        char dev[64];
        unsigned long long reads, read_merges, read_sectors, read_ms;
        unsigned long long writes, write_merges, write_sectors, write_ms;
        
        if (sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu",
            &major, &minor, dev, 
            &reads, &read_merges, &read_sectors, &read_ms,
            &writes, &write_merges, &write_sectors, &write_ms) >= 11) {
            
            /* Filter out loop and ram devices to get real hardware IO */
            if (strncmp(dev, "loop", 4) != 0 && strncmp(dev, "ram", 3) != 0) {
                total_ios += (reads + writes);
            }
        }
    }
    fclose(fp);
    *ios_out = total_ios;
    return 1;
}

static int sys_disk_io_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;
    test_result_t *r = &results[0];
    *out_count = 1;
    
    safe_copy(r->type, cfg->type, CONFIG_MAX_STR);
    safe_copy(r->display_name, cfg->display_name, CONFIG_MAX_STR);
    r->timestamp = get_timestamp();

    unsigned long long io1, io2;
    
    if (!get_global_io(&io1)) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to read /proc/diskstats", sizeof(r->detail)); return 0;
    }

    usleep(100000); /* 100ms sample window */

    if (!get_global_io(&io2)) {
        r->ok = 0; r->value = 0; safe_copy(r->detail, "Failed to read /proc/diskstats (2nd pass)", sizeof(r->detail)); return 0;
    }

    unsigned long long io_diff = io2 - io1;
    double iops = io_diff * 10.0; /* Multiply by 10 to extrapolate 100ms to 1 second */

    r->ok = 1;
    r->value = iops;
    safe_copy(r->unit, "iops", sizeof(r->unit));
    snprintf(r->detail, sizeof(r->detail), "Global Disk I/O: %.0f IOPS", iops);

    return 0;
}

const test_module_t mod_sys_cpu        = { .type_name = "cpu",          .collect = sys_cpu_collect };
const test_module_t mod_sys_ram        = { .type_name = "ram",          .collect = sys_ram_collect };
const test_module_t mod_sys_load       = { .type_name = "load_average", .collect = sys_load_collect };
const test_module_t mod_sys_disk_usage = { .type_name = "disk_usage",   .collect = sys_disk_usage_collect };
const test_module_t mod_sys_disk_io    = { .type_name = "disk_io",      .collect = sys_disk_io_collect };

void register_sys_tests(void) {
    framework_register(&mod_sys_cpu);
    framework_register(&mod_sys_ram);
    framework_register(&mod_sys_load);
    framework_register(&mod_sys_disk_usage);
    framework_register(&mod_sys_disk_io);
}
