# Module Configuration Reference

This reference outlines all available telemetry checks for the `nxtmon-slave-daemon`.
The `type` field must be written exactly as shown below for the daemon to recognize the test.

## Global Configuration Structure

```yaml
schema_version: 2

agent:
  display_name: "my-reference-node"

tests:
```

## 1. System & Hardware Metrics
*All of these tests are fully implemented and require no special parameters.*

```yaml
  - type: cpu
    display_name: "CPU Usage"

  - type: ram
    display_name: "RAM Usage"

  # Emits three separate metrics in the JSON payload for 1m, 5m, and 15m
  - type: load_average
    display_name: "System Load Averages"

  - type: disk_io
    display_name: "Disk I/O Metrics"

  # extra_val defines the mount path. Defaults to "/" if omitted.
  - type: disk_usage
    display_name: "Root Partition Usage"
    extra_val: "/"
```

## 2. OS Services & Load Balancer
*These tests are implemented and require `extra_val` or `hosts` arrays.*

```yaml
  # extra_val = systemd service name, typically without ".service"
  - type: service_status
    display_name: "MariaDB Service State"
    extra_val: "mariadb"

  # extra_val = process name as visible in /proc or ps (e.g., redis-server)
  - type: service_pid
    display_name: "Redis PID Check"
    extra_val: "redis-server"

  # extra_val = absolute path to HAProxy runtime/admin socket
  - type: lb_backend_state
    display_name: "Nextcloud App Nodes Backend State"
    extra_val: "/run/haproxy/admin.sock"

  # hosts.host = DNS name of target certificate endpoint
  # hosts.port is usually 443
  - type: ssl_cert
    display_name: "Main Domain SSL Expiry"
    hosts:
      - host: "cloud.example.com"
        port: 443
```

## 3. Databases (MariaDB/MySQL)
*All DB tests require authentication. Use `extra_key: auth` and provide `user:password` in `extra_val`.*

```yaml
  - type: db_latency_write
    display_name: "Database Write Latency"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_latency_read
    display_name: "Database Read Latency"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_master_slave
    display_name: "Replication Role Check"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_rw_ro
    display_name: "Read-Write / Read-Only State"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_gtid
    display_name: "Current GTID Position"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_connections
    display_name: "Available DB Connections"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306

  - type: db_innodb_buffer
    display_name: "InnoDB Buffer Pool Hit Rate"
    extra_key: "auth"
    extra_val: "monitor:secret123"
    hosts:
      - host: "127.0.0.1"
        port: 3306
```

## 4. In-Memory Stores (Redis)
*Requires `extra_key: auth` and the raw Redis password in `extra_val`.*

```yaml
  - type: redis_sync_channel
    display_name: "Redis Sync Channel Status"
    extra_key: "auth"
    extra_val: "redis_password"
    hosts:
      - host: "127.0.0.1"
        port: 6379

  - type: redis_sync_status
    display_name: "Redis Replication Lag"
    extra_key: "auth"
    extra_val: "redis_password"
    hosts:
      - host: "127.0.0.1"
        port: 6379

  - type: redis_memory
    display_name: "Redis Memory Usage"
    extra_key: "auth"
    extra_val: "redis_password"
    hosts:
      - host: "127.0.0.1"
        port: 6379

  - type: redis_evicted_keys
    display_name: "Evicted Keys Rate"
    extra_key: "auth"
    extra_val: "redis_password"
    hosts:
      - host: "127.0.0.1"
        port: 6379

  - type: redis_clients
    display_name: "Connected Redis Clients"
    extra_key: "auth"
    extra_val: "redis_password"
    hosts:
      - host: "127.0.0.1"
        port: 6379
```

## 5. Web & PHP-FPM

```yaml
  - type: web_type
    display_name: "Webserver Type Check"
    hosts:
      - url: "http://127.0.0.1/server-status"

  - type: php_fpm_workers
    display_name: "PHP-FPM Worker Saturation"
    hosts:
      - url: "http://127.0.0.1/fpm-status?json"

  - type: php_fpm_queue
    display_name: "PHP-FPM Listen Queue"
    hosts:
      - url: "http://127.0.0.1/fpm-status?json"

  - type: php_opcache
    display_name: "OPCache Hit Rate & Memory"
    hosts:
      - url: "http://127.0.0.1/opcache-status.php"

  - type: web_response_time
    display_name: "Loopback Web Response Time"
    hosts:
      - url: "http://127.0.0.1/status.php"
```

## 6. Storage (NFS)

```yaml
  # extra_val = exported path on the NFS server
  - type: nfs_exports
    display_name: "NFS Exports Availability"
    extra_val: "/srv/nfs/nextcloud"

  # extra_val = local mount point on the NFS client
  - type: nfs_mounts
    display_name: "NFS Client Mounts"
    extra_val: "/mnt/nfs_share"

  # extra_val = writable mounted directory used for temp read/write test
  - type: nfs_rw_test
    display_name: "NFS Read/Write Validation"
    extra_val: "/mnt/nfs_share"

  - type: nfs_iowait
    display_name: "NFS IO Wait Time"

  # extra_val = directory path whose inode usage should be measured
  - type: nfs_inode
    display_name: "NFS Server Inode Usage"
    extra_val: "/mnt/nfs_share"
```

## 7. Nextcloud Application

```yaml
  # extra_val = path to cron marker or lock file used by your implementation
  - type: nc_cron
    display_name: "Cron Execution Timestamp"
    extra_val: "/var/www/nextcloud/data/cron.lock"

  # extra_val = absolute path to occ binary or wrapper command
  - type: nc_users
    display_name: "Active Users / Logged-in Sessions"
    extra_val: "/var/www/nextcloud/occ"

  # extra_val = absolute path to nextcloud.log
  - type: nc_log_errors
    display_name: "Nextcloud Log Error Rate"
    extra_val: "/var/www/nextcloud/data/nextcloud.log"
```

***

## 8. Planned Modules (Currently Unregistered)
*The following module types are planned but currently return `"Test execution failed or module type unregistered"` in this daemon build.*

```yaml
  # Network & DNS
  - type: hostname
  - type: ip-address
  - type: net_load
  - type: dns_resolve
  - type: ping
  - type: conntrack
  - type: service_port
  - type: service_connections

  # Advanced Load Balancer
  - type: lb_sticky_session
  - type: lb_connections
  - type: lb_cache

  # Advanced Database
  - type: db_sync_status
  - type: db_slow_queries

  # Advanced Nextcloud API
  - type: url_reachable
```
