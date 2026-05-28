# nxtmon Test Execution Framework

The `nxtmon-slave-daemon` uses a modular, role-agnostic execution framework. Instead of a hardcoded monolithic polling loop, the daemon parses the YAML configuration and maps the `type` of each test (e.g., `cpu`, `url_reachable`) to a registered C function using a virtual table (vtable) approach.

This document explains the C framework API and how to implement new test libraries.

## Architecture Overview

1. **Test Modules:** Isolated C files (e.g., `tests_os.c`, `tests_net.c`) that define one or more atomic tests.
2. **The Registry:** A central array in `framework.c` that holds pointers to available tests.
3. **Execution:** The main loop iterates over the `cfg->tests` array, looks up the requested test `type` in the registry, and calls its `collect` function.
4. **Standardized Output:** Every test maps its internal metrics to an array of generic `test_result_t` structures.

## Core Data Structures

To write a test, you must interact with two primary structures defined in `framework.h`:

### 1. `test_result_t`
This struct represents exactly one metric payload. Tests that check multiple sub-items (like multiple hosts in `url_reachable`) will return an array of these.

```c
typedef struct {
    char type[CONFIG_MAX_STR];        // Matches the test type (e.g., "cpu")
    char display_name[CONFIG_MAX_STR];// The human-readable name from the YAML config
    int ok;                           // 1 = success, 0 = failure, -1 = unknown
    double value;                     // The actual numeric metric
    char unit;                    // Unit identifier (e.g., "fraction", "ms", "http_status")
    char detail;                 // Human-readable context or error message
    long long timestamp;              // Epoch timestamp of execution
} test_result_t;
```

### 2. `test_module_t`
This is the interface contract your test must fulfill to be registered.

```c
typedef struct {
    const char *type_name;    // The YAML identifier (e.g., "cpu", "tcp_connect")
    test_init_fn init;        // Optional: Called once during daemon startup
    test_collect_fn collect;  // Mandatory: Called every interval to gather data
    test_shutdown_fn shutdown;// Optional: Called during graceful exit
} test_module_t;
```

## How to Write a New Test

Here is a step-by-step guide to implementing a new check, using a dummy `cpu` check as an example.

### Step 1: Write the Collect Function
The `collect` function receives the specific configuration for this test block (`cfg`), a pointer to write results into (`results`), the maximum allowed results, and a pointer to write how many results were actually generated (`out_count`).

```c
#include "framework.h"
#include <time.h>
#include <string.h>

int my_cpu_collect(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count) {
    if (max_results < 1) return -1;

    // 1. Perform your actual measurement here (e.g., read /proc/stat)
    double current_cpu_usage = 0.23; 
    int fetch_success = 1;

    // 2. Populate the standard result struct
    test_result_t *res = &results;
    
    // Copy context from the config
    strncpy(res->type, cfg->type, sizeof(res->type)-1);
    strncpy(res->display_name, cfg->display_name, sizeof(res->display_name)-1);
    
    // Set measurement data
    res->ok = fetch_success ? 1 : 0;
    res->value = current_cpu_usage;
    strncpy(res->unit, "fraction", sizeof(res->unit)-1);
    strncpy(res->detail, fetch_success ? "average since last cycle" : "failed to read /proc", sizeof(res->detail)-1);
    res->timestamp = (long long)time(NULL);

    // 3. Update the count
    *out_count = 1;
    
    return 0; // Return 0 on execution success (even if the check itself "failed")
}
```

### Step 2: Define the Module Structure
Create the `test_module_t` struct wrapping your functions.

```c
const test_module_t module_cpu = {
    .type_name = "cpu",
    .init = NULL,             // No special startup needed
    .collect = my_cpu_collect,
    .shutdown = NULL
};
```

### Step 3: Register the Module
During the daemon's bootstrap phase in `main.c`, initialize the framework and register your module before starting the main polling loop.

```c
#include "framework.h"

extern const test_module_t module_cpu;
// extern const test_module_t module_tcp_connect;

int main() {
    framework_init();
    
    // Register all available tests
    framework_register(&module_cpu);
    // framework_register(&module_tcp_connect);
    
    // ... load config and start main loop ...
    
    framework_shutdown();
    return 0;
}
```

## Development Guidelines

- **Handle Multiple Hosts:** If a test supports the `hosts[]` YAML array (like `url_reachable`), the `collect` function must iterate over `cfg->hosts`, perform the check for each, and populate `results[0]`, `results[1]`, etc., up to `cfg->host_count`. Update `*out_count` to match.
- **Non-blocking Operations:** Do not use blocking network calls without timeouts. Use `select()`, `poll()`, or library-specific timeout settings (e.g., `CURLOPT_TIMEOUT`) so one hanging Nextcloud backend doesn't freeze the entire daemon.
- **Fail Gracefully:** If a check fails (e.g., connection refused), `collect` should still return `0` (meaning the framework executed the test successfully). Indicate the failure by setting `res->ok = 0` and explaining why in `res->detail`. Only return `-1` from `collect` for fatal internal errors (like null pointers).
- **Categorize Files:** Keep tests organized in domains. Put all OS-level metrics in `tests_os.c`, MySQL checks in `tests_db.c`, and network checks in `tests_net.c`.
