# nxtmon-slave-daemon

A high-performance, ultra-lightweight telemetry agent for the nxtmon monitoring suite. 

Written in pure C, this daemon is designed to gather critical infrastructure, database, and application metrics and push them securely over TLS to a central master node. To absolutely guarantee zero memory leaks over months of uptime, the daemon is designed as a single-pass executable driven by a `systemd` scheduling loop.

## 🚀 Features & Telemetry Modules

The daemon uses a modular vtable framework to dynamically execute checks defined in the configuration file.

*   **System (Hardware):** CPU usage, RAM fraction, 1/5/15m Load Averages, Disk Usage, Disk IOPS (via `/proc`).
*   **Network & OS:** TCP connectivity latency, DNS resolution, systemd unit states, PID checks.
*   **Databases (MariaDB/MySQL):** Read/Write latency, Replication Role (Master/Slave), Read-Only state, GTID position, InnoDB Buffer Pool hit rate, Active connections.
*   **In-Memory (Redis):** Replication lag, Memory usage, Evicted keys rate, Connected clients.
*   **Load Balancing (HAProxy):** Direct Unix socket backend state polling, Domain SSL certificate expiration checks.
*   **Web & PHP:** PHP-FPM worker saturation, Listen queues, OPCache memory hit rates, Loopback response times.
*   **Application (Nextcloud):** OCS Serverinfo API health, Cron execution latency, Log error rate counting, Active user tracking.
*   **Storage (NFS):** Export availability, Client mount verification, Read/Write IO latency validation, Inode usage.

---

## 📦 Installation

We provide an automated installer that checks for dependencies, dynamically builds the binary from source, places the configuration files, and sets up the systemd service.

Run the following command on the target Linux machine (Debian/Ubuntu recommended):

```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/nxtmon-slave-daemon/main/install.sh)"
```

The installer will prompt you for:
1.  Installing missing `apt` dependencies (libcurl, libmariadb, libhiredis, etc.).
2.  Compiling the `nxtmon-slave` binary.
3.  Installing it to `/usr/local/bin` and copying the default config to `/etc/nxtmon/config.yaml`.
4.  Setting the polling interval (default: 10s) and verbosity (to prevent systemd log spam).

---

## ⚙️ Configuration

The daemon is driven entirely by a YAML configuration file located at:
`/etc/nxtmon/config.yaml`

You can view all possible module configurations in the provided [`doc/reference_config.yaml`](doc/reference_config.yaml). 

**Key Configuration Blocks:**
*   `agent`: Defines the display name and tags of the node.
*   `master`: Defines the IP, Port, and Bearer Token for the central nxtmon receiver.
*   `tests`: An array of module invocations. Each test requires a `type` matching an internal C module. Depending on the test, it may require `hosts` (IP/Port), or `extra_val` (file paths, sockets, passwords).

---

## 🛠️ Service Management

The daemon runs as a `systemd` service. The interval at which it runs is defined by the `RestartSec=` parameter in its unit file.

**Check if the daemon is running cleanly:**
```bash
sudo systemctl status nxtmon-slave
```
*(Note: Seeing `inactive (dead)` with `code=exited, status=0/SUCCESS` momentarily is normal. The binary runs instantly and exits, waiting for the systemd timer to restart it).*

**View live telemetry logs:**
recommended to use | jq . for clarity
```bash
sudo journalctl -u nxtmon-slave -f
```

**Restart the daemon after changing the YAML config:**
```bash
sudo systemctl restart nxtmon-slave
```

**Uninstall the daemon completely:**
```bash
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/nxtmon-slave-daemon/main/install.sh)" -- --uninstall
```

---

## 📡 API Schema Specification (V2)

When the daemon completes its checks, it packages the metrics into a minified JSON payload and securely `POST`s it to the master node (`https://<master-ip>/api/telemetry`).

The payload conforms to **Schema Version 2**:

```json
{
  "schema_version": 2,
  "timestamp": 1779915971,
  "agent": {
    "display_name": "dbmaster-node-01",
    "hostname": "dbmaster"
  },
  "results": [
    {
      "type": "ram",
      "display_name": "RAM Usage",
      "ok": true,
      "value": 0.02437,
      "unit": "fraction",
      "detail": "RAM: 0.8 GB used / 31.3 GB total",
      "timestamp": 1779915971
    },
    {
      "type": "service_pid",
      "display_name": "Redis PID Check",
      "ok": false,
      "value": 0,
      "unit": "pid",
      "detail": "Process redis-server not found",
      "timestamp": 1779915969
    },
    {
      "type": "lb_cache",
      "display_name": "Proxy Cache Hit Ratio",
      "ok": null,
      "value": 0,
      "unit": null,
      "detail": "Test execution failed or module type unregistered",
      "timestamp": 1779915969
    }
  ]
}
```

### Data Types
*   **`ok` (Boolean/Null):** 
    *   `true` (1): The check passed successfully.
    *   `false` (0): The check failed (e.g., service offline, threshold exceeded).
    *   `null` (-1): The check crashed, couldn't be executed, or the module `type` was not registered in the C framework.
*   **`value` (Float):** The raw numerical metric (ms latency, fractions 0.0-1.0, counts).
*   **`unit` (String):** Maps to frontend UI formatters (`ms`, `fraction`, `iops`, `pid`, `count`).

---

## 👨‍💻 Development & Compilation

To build the daemon manually for development without the installer:

**1. Install Dependencies (Debian/Ubuntu):**
```bash
sudo apt-get update
sudo apt-get install build-essential libyaml-dev libcurl4-openssl-dev libmariadb-dev libmariadb-dev-compat libhiredis-dev libcjson-dev libssl-dev
```

**2. Compile:**
```bash
make
```

**3. Run Locally (Dumps JSON to stdout instead of pushing):**
To test new modules without sending data to a master node, run the executable directly. It will detect manual execution and print the JSON payload to the console:
```bash
./nxtmon-slave doc/reference_config.yaml | jq .
```

### Adding a New Module
1. Create your logic in a new `src/tests_custom.c` file.
2. Follow the `static int my_collect_func(const test_entry_t *cfg, test_result_t *results, size_t max_results, size_t *out_count)` signature.
3. Expose it via a registry function (`register_custom_tests()`).
4. Include and call your registry function in `src/main.c` before the YAML parser triggers.
