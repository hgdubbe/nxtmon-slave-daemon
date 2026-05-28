# Module Reference & Expected Output

This document outlines every telemetry module available in `nxtmon-slave-daemon`, how to configure it in `/etc/nxtmon/config.yaml`, and the expected JSON data it yields.

## 1. System & Hardware (`tests_sys.c`)
Requires no special configuration parameters.

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `cpu` | None | `0.0` - `1.0` | `fraction` | `true` if read succeeds | "CPU Load: 12.4%" |
| `ram` | None | `0.0` - `1.0` | `fraction` | `true` if read succeeds | "RAM: 1.2 GB used / 16.0 GB total" |
| `load_average` | None | E.g. `0.45` | `load` | `true` | Emits 3 results: "(1m)", "(5m)", "(15m)" |
| `disk_usage` | `extra_val`: mount path (default: `/`) | `0.0` - `1.0` | `fraction` | `true` if path exists | "Disk: 45.1 GB used / 100.0 GB total (/)" |
| `disk_io` | None | Total IO/sec | `iops` | `true` if read succeeds | "Global Disk I/O: 145 IOPS" |

---

## 2. Network & OS (`tests_net.c`, `tests_services.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `hostname` | None | `0.0` | `string` | `true` | Returns current hostname in detail |
| `tcp_connect` | `hosts[0].host`, `hosts[0].port` | Latency (ms) | `ms` | `true` if port open, `false` if timeout | "Connected in 1.45 ms" |
| `service_status` | `extra_val`: Service name (e.g. `nginx`) | `1.0` / `0.0` | `boolean` | `true` if active/running | "Unit nginx.service is active" |
| `service_pid` | `extra_val`: Process name | `PID` integer | `pid` | `true` if PID > 0 | "Process redis-server running with PID 948" |

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

---

## 4. In-Memory: Redis (`tests_redis.c`)
Requires `hosts[0].host`, `hosts[0].port`. Authentication is passed as password via `extra_val`.

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `redis_sync_status` | Redis Auth | Sync Lag (bytes) | `bytes` | `true` if lag < threshold | "Replication lag: 1024 bytes" |
| `redis_memory` | Redis Auth | `0.0` - `1.0` | `fraction` | `true` | "Memory: 1.5 GB used / 4.0 GB max" |
| `redis_evicted_keys`| Redis Auth | Eviction Count | `count` | `true` if 0, `false` if >0 | "Evicted keys: 0" |
| `redis_clients` | Redis Auth | Count | `count` | `true` | "Connected clients: 12" |

---

## 5. Web & PHP-FPM (`tests_web.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `web_response_time` | `hosts[0].url` | Latency (ms) | `ms` | `true` if HTTP 200/300 | "Response time: 45.2 ms" |
| `php_fpm_workers` | `hosts[0].url` (FPM status page) | `0.0` - `1.0` | `fraction` | `true` if sat. < 80% | "FPM Workers: 10 active / 50 total" |
| `php_fpm_queue` | `hosts[0].url` | Count | `count` | `true` if queue == 0 | "FPM Listen Queue: 0" |
| `php_opcache` | `hosts[0].url` (Custom PHP script) | `0.0` - `1.0` | `fraction` | `true` | "OPCache Memory Hit: 98.4%" |

---

## 6. Application: Nextcloud (`tests_nextcloud.c`, `tests_nextcloud_api.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `nc_cron` | `extra_val`: path to cron file | Seconds since last run | `seconds` | `true` if < 900s | "Cron last ran 120 seconds ago" |
| `nc_log_errors` | `extra_val`: path to nextcloud.log | Error Count | `count` | `true` | "Found 0 errors in last 50 log lines" |
| `nc_serverinfo_api`| `hosts[0].url`, `extra_val`: `user:pass`| HTTP Status Code | `status` | `true` if 100/200 | "Serverinfo API reports OK" |
| `nc_users` | `extra_val`: path to `occ` | Count | `count` | `true` | "Active users: 4" (via API bonus block) |

---

## 7. Storage: NFS (`tests_nfs.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `nfs_exports` | `extra_val`: Exported path | `1.0` | `boolean` | `true` if in `/var/lib/nfs/etab` | "Export found: /mnt/share" |
| `nfs_mounts` | `extra_val`: Mounted path | `1.0` | `boolean` | `true` if in `/proc/mounts` | "NFS mount active: /mnt/client" |
| `nfs_rw_test` | `extra_val`: Mounted path | Latency (ms) | `ms` | `true` if read/write succeeds | "NFS IO cycle completed in 2.1 ms" |
| `nfs_iowait` | None | `0.0` - `1.0` | `fraction` | `true` | "IO Wait is 0.5%" |
| `nfs_inode` | `extra_val`: Mount path | `0.0` - `1.0` | `fraction` | `true` | "Inode usage: 45%" |

---

## 8. Load Balancer: HAProxy (`tests_lb.c`)

| Type | Configuration | Output `value` | `unit` | `ok` Status | `detail` Example |
|---|---|---|---|---|---|
| `ssl_cert` | `hosts[0].host` (Domain) | Days until expiry | `days` | `true` if > 14 days | "SSL valid for 45 days" |
| `lb_backend_state` | `extra_val`: Path to AF_UNIX socket | Total UP nodes | `count` | `true` if > 0 | "Backend app_nodes: 3 UP, 0 DOWN" |
