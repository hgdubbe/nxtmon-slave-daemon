# Module Reference & Expected Output

This document outlines every telemetry module available in `nxtmon-slave-daemon`, how to configure it in `/etc/nxtmon/config.yaml`, and the expected JSON data it yields.

## Understanding `extra_key` and `extra_val`
Because the daemon is written in strict C, the configuration struct (`test_entry_t`) needs a flexible way to accept arbitrary parameters that don't fit into standard network fields like `host` or `port`. 
* **`extra_val`**: A generic string field used to pass the primary argument to a module. Depending on the test, this could be a file path, a systemd service name, a Unix socket, or a password.
* **`extra_key`**: Acts as an identifier to tell the C module how to parse `extra_val`. For example, setting `extra_key: auth` explicitly tells the database or API module that `extra_val` contains authentication credentials.

---

## 1. System & Hardware (`tests_sys.c`)
Requires no special configuration parameters.

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `cpu` | None | `0.0` - `1.0` | `fraction` | `true` if read succeeds | "CPU Load: 12.4%" |
| `ram` | None | `0.0` - `1.0` | `fraction` | `true` if read succeeds | "RAM: 1.2 GB used / 16.0 GB total" |
| `load_average` | None | E.g. `0.45` | `load` | `true` | Emits 3 results: "(1m)", "(5m)", "(15m)" |
| `disk_usage` | `extra_val`: mount path (default: `/`) | `0.0` - `1.0` | `fraction` | `true` if path exists | "Disk: 45.1 GB used / 100.0 GB total (/)" |
| `disk_io` | None | Total IO/sec | `iops` | `true` if read succeeds | "Global Disk I/O: 145 IOPS" |

### Usage Examples
```yaml
# Basic system checks (no extra parameters needed)
- type: cpu
  display_name: System CPU Usage

- type: ram
  display_name: System RAM Usage

- type: load_average
  display_name: System Load Averages

# Disk usage: Checking the default root partition
- type: disk_usage
  display_name: Root Partition Usage

# Disk usage: Checking a specific secondary mount point
# extra_val defines the absolute path to the mount directory
- type: disk_usage
  display_name: Database Storage Usage
  extra_val: "/mnt/db_data"

- type: disk_io
  display_name: Global Disk IOPS
```

---

## 2. Network & OS (`tests_net.c`, `tests_services.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `hostname` | None | `0.0` | `string` | `true` | Returns current hostname in detail |
| `tcp_connect` | `hosts[0].host`, `hosts[0].port` | Latency (ms) | `ms` | `true` if port open, `false` if timeout | "Connected in 1.45 ms" |
| `service_status` | `extra_val`: Service name (e.g. `nginx`) | `1.0` / `0.0` | `boolean` | `true` if active/running | "Unit nginx.service is active" |
| `service_pid` | `extra_val`: Process name | `PID` integer | `pid` | `true` if PID > 0 | "Process redis-server running with PID 948" |

### Usage Examples
```yaml
# Check local hostname resolution
- type: hostname
  display_name: Local Hostname Check

# TCP Connect: Single host target
- type: tcp_connect
  display_name: Ping Core Router
  hosts:
    - host: 192.168.1.1
      port: 80

# TCP Connect: Multi-host targeting (Requires separate test blocks)
- type: tcp_connect
  display_name: Check App Node 1 SSH
  hosts:
    - host: 10.0.0.11
      port: 22

- type: tcp_connect
  display_name: Check App Node 2 SSH
  hosts:
    - host: 10.0.0.12
      port: 22

# Check if a systemd service is active
# extra_val defines the exact name of the systemd unit (without .service)
- type: service_status
  display_name: HAProxy Service State
  extra_val: haproxy

# Extract the PID of a running process
# extra_val defines the exact process name as it appears in /proc/PID/comm
- type: service_pid
  display_name: Redis PID Check
  extra_val: redis-server
```

---

## 3. Database: MariaDB / MySQL (`tests_db.c`)
Requires `hosts[0].host`, `hosts[0].port`. Authentication is passed as `user:password` via `extra_val`.

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `db_latency_read` | DB Auth | Latency (ms) | `ms` | `true` on SELECT 1 | "Read latency: 0.34 ms" |
| `db_latency_write` | DB Auth | Latency (ms) | `ms` | `true` on INSERT/DELETE | "Write latency: 1.12 ms" |
| `db_master_slave` | DB Auth | `1.0` (Master) / `0.0` (Slave) | `role` | `true` | "Role: SLAVE" |
| `db_rw_ro` | DB Auth | `1.0` (RW) / `0.0` (RO) | `mode` | `true` | "Mode: READ-ONLY (read_only=ON)" |
| `db_gtid` | DB Auth | `0.0` | `gtid` | `true` | "GTID: 0-1-45938" |
| `db_innodb_buffer`| DB Auth | `0.0` - `1.0` | `fraction` | `true` | "InnoDB Buffer Hit Rate: 99.8%" |
| `db_connections` | DB Auth | Count | `count` | `true` | "Connections: 45 / 500 max" |

### Usage Examples
```yaml
# Common block for a local database
# extra_key flags the parameter as authentication
# extra_val contains the literal "username:password" string
- type: db_latency_read
  display_name: Local DB Read Latency
  extra_key: auth
  extra_val: "monitor_user:securepassword123"
  hosts:
    - host: 127.0.0.1
      port: 3306

# Checking replication role on a remote cluster node
- type: db_master_slave
  display_name: Remote Node Replication Role
  extra_key: auth
  extra_val: "monitor_user:securepassword123"
  hosts:
    - host: 10.0.0.50
      port: 3306

# Checking if the database is locked to Read-Only mode
- type: db_rw_ro
  display_name: Database Read-Only State
  extra_key: auth
  extra_val: "monitor_user:securepassword123"
  hosts:
    - host: 127.0.0.1
      port: 3306
```

---

## 4. In-Memory: Redis (`tests_redis.c`)
Requires `hosts[0].host`, `hosts[0].port`. Authentication is passed as password via `extra_val`.

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `redis_sync_status` | Redis Auth | Sync Lag (bytes) | `bytes` | `true` if lag < threshold | "Replication lag: 1024 bytes" |
| `redis_memory` | Redis Auth | `0.0` - `1.0` | `fraction` | `true` | "Memory: 1.5 GB used / 4.0 GB max" |
| `redis_evicted_keys`| Redis Auth | Eviction Count | `count` | `true` if 0, `false` if >0 | "Evicted keys: 0" |
| `redis_clients` | Redis Auth | Count | `count` | `true` | "Connected clients: 12" |

### Usage Examples
```yaml
# Local Redis without a password
- type: redis_memory
  display_name: Redis Memory Saturation
  hosts:
    - host: 127.0.0.1
      port: 6379

# Remote Redis with a password
# extra_key flags the parameter as authentication
# extra_val contains the literal Redis AUTH password
- type: redis_sync_status
  display_name: Redis Sentinel Replication Lag
  extra_key: auth
  extra_val: "my_redis_password"
  hosts:
    - host: 10.0.0.60
      port: 6379
```

---

## 5. Web & PHP-FPM (`tests_web.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `web_response_time` | `hosts[0].url` | Latency (ms) | `ms` | `true` if HTTP 200/300 | "Response time: 45.2 ms" |
| `php_fpm_workers` | `hosts[0].url` (FPM status page) | `0.0` - `1.0` | `fraction` | `true` if sat. < 80% | "FPM Workers: 10 active / 50 total" |
| `php_fpm_queue` | `hosts[0].url` | Count | `count` | `true` if queue == 0 | "FPM Listen Queue: 0" |
| `php_opcache` | `hosts[0].url` (Custom PHP script) | `0.0` - `1.0` | `fraction` | `true` | "OPCache Memory Hit: 98.4%" |

### Usage Examples
```yaml
# Standard HTTP response time check
- type: web_response_time
  display_name: Public Site Responsiveness
  hosts:
    - url: "https://mycloud.domain.com"

# PHP-FPM status page check (Assuming you exposed /status locally)
- type: php_fpm_workers
  display_name: PHP-FPM Worker Pool
  hosts:
    - url: "http://127.0.0.1/status?json"
```

---

## 6. Application: Nextcloud (`tests_nextcloud.c`, `tests_nextcloud_api.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `nc_cron` | `extra_val`: path to cron file | Seconds since last run | `seconds` | `true` if < 900s | "Cron last ran 120 seconds ago" |
| `nc_log_errors` | `extra_val`: path to nextcloud.log | Error Count | `count` | `true` | "Found 0 errors in last 50 log lines" |
| `nc_serverinfo_api`| `hosts[0].url`, `extra_val`: `user:pass`| HTTP Status Code | `status` | `true` if 100/200 | "Serverinfo API reports OK" |
| `nc_users` | `extra_val`: path to `occ` | Count | `count` | `true` | "Active users: 4" (via API bonus block) |

### Usage Examples
```yaml
# Check if Nextcloud's cron.php executed recently (local file stat)
# extra_val points to the marker file modified by cron
- type: nc_cron
  display_name: Nextcloud Cron Execution
  extra_val: "/var/www/nextcloud/data/cron.lock"

# Parse recent error rates directly from the Nextcloud log
# extra_val points to the absolute path of nextcloud.log
- type: nc_log_errors
  display_name: Nextcloud Log Health
  extra_val: "/var/www/nextcloud/data/nextcloud.log"

# Hit the Nextcloud App API (Requires app password)
# extra_key flags the parameter as authentication
# extra_val contains the literal "admin_user:app_password" string for HTTP Basic Auth
- type: nc_serverinfo_api
  display_name: Nextcloud Serverinfo API
  extra_key: auth
  extra_val: "admin:app_password_here"
  hosts:
    - url: "https://cloud.domain.com/ocs/v2.php/apps/serverinfo/api/1.0/info?format=json"
```

---

## 7. Storage: NFS (`tests_nfs.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `nfs_exports` | `extra_val`: Exported path | `1.0` | `boolean` | `true` if in `/var/lib/nfs/etab` | "Export found: /mnt/share" |
| `nfs_mounts` | `extra_val`: Mounted path | `1.0` | `boolean` | `true` if in `/proc/mounts` | "NFS mount active: /mnt/client" |
| `nfs_rw_test` | `extra_val`: Mounted path | Latency (ms) | `ms` | `true` if read/write succeeds | "NFS IO cycle completed in 2.1 ms" |
| `nfs_iowait` | None | `0.0` - `1.0` | `fraction` | `true` | "IO Wait is 0.5%" |
| `nfs_inode` | `extra_val`: Mount path | `0.0` - `1.0` | `fraction` | `true` | "Inode usage: 45%" |

### Usage Examples
```yaml
# ON THE NFS SERVER: Check if the directory is actually exported
# extra_val is the local absolute path that should be network-exported
- type: nfs_exports
  display_name: NFS Exports Check
  extra_val: "/mnt/nfs_share"

# ON THE NFS CLIENT: Check if it is successfully mounted
# extra_val is the absolute path where the client mounts the share
- type: nfs_mounts
  display_name: NFS Client Mount State
  extra_val: "/var/www/nextcloud/data"

# ON THE NFS CLIENT: Perform an actual Read/Write/Delete cycle over the network
# extra_val is the mount point where the test file will be temporarily written
- type: nfs_rw_test
  display_name: NFS Network R/W Latency
  extra_val: "/var/www/nextcloud/data"
```

---

## 8. Load Balancer: HAProxy (`tests_lb.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `ssl_cert` | `hosts[0].host` (Domain) | Days until expiry | `days` | `true` if > 14 days | "SSL valid for 45 days" |
| `lb_backend_state` | `extra_val`: Path to AF_UNIX socket | Total UP nodes | `count` | `true` if > 0 | "Backend app_nodes: 3 UP, 0 DOWN" |

### Usage Examples
```yaml
# Check the expiration date of your main SSL certificate
- type: ssl_cert
  display_name: Domain SSL Expiry
  hosts:
    - host: "mycloud.domain.com"
      port: 443

# Query HAProxy's admin socket to verify backend servers are UP
# extra_val is the absolute path to the HAProxy runtime AF_UNIX socket
- type: lb_backend_state
  display_name: HAProxy Backend Node Health
  extra_val: "/run/haproxy/admin.sock"
```
