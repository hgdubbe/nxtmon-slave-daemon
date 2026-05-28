#nxtmon-slave-daemon

nxtmon Slave Daemon - AI Implementation Specification
Role: The slave daemon (nxtmond --role slave) is a modular, lightweight data-collection agent running on monitored Linux hosts. It executes a dynamically configured list of generic tests, formats the results into a standardized JSON payload, and pushes them to a central master daemon via TCP.

install with:
sudo bash -c "$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/nxtmon-slave-daemon/main/install.sh)"

enable/disable/start/stop/status service
sudo systemctl enable/disable/start/stop/status nxtmon-slave.service

Architecture Paradigm: The slave has no concept of "Host Roles" (e.g., it does not know what a "loadbalancer" is). It only knows how to execute atomic "Tests" (e.g., cpu, url_reachable, tcp_connect) defined in a YAML configuration file.
--test / --dry-run: load config, run all tests once, print JSON to stdout.

--dump-config: parse and re-emit the effective config for debugging. validate config

Self-monitoring:

Lightweight logging (to syslog or file) with log-level flag, especially for connection failures.
1. Configuration Engine (YAML)
The daemon parses a YAML configuration file (using libyaml) containing:

Agent Metadata: display_name (nickname), master host/port, and interval_sec.

Test Sequence: A list of tests. Each test requires a type (maps to an internal C function) and a display_name. Some tests require specific arguments (e.g., url for HTTP checks, host/port for TCP checks).

Example YAML Structure to Support:

text
master:
  host: 10.0.0.10
  port: 9000
agent:
  display_name: myloadbalancer
  interval_sec: 10
tests:
  - type: hostname
    display_name: Local Hostname
  - type: cpu
    display_name: CPU Usage
  - type: url_reachable
    display_name: Nextcloud1 Reachability
    hosts:
      - display_name: nextcloud1
        url: https://nextcloud1.xyz.local/status.php
  - type: url_reachable
    display_name: Nextcloud2 Reachability
    hosts:
      - display_name: nextcloud1
        url: https://nextcloud1.xyz.local/status.php
2. Test Execution Framework (C Implementation)
The core logic must use a Test Registry (function pointer vtable approach) rather than hardcoded monoliths.

Signature: Each test module implements an init/collect/shutdown interface.

Execution: In the main loop, the daemon iterates through the loaded YAML tests array, calling the mapped collect function for each.

Standardized Output: Every test function returns an array of generic test_result_t structs containing:

type (string)

display_name (string)

ok (boolean/int: 1=success, 0=fail, -1=unknown)

value (double/float for metrics, or mapped to string)

unit (string, e.g., "fraction", "ms", "http_status", or null)

detail (string, human-readable context/errors)

3. Core Test Types to Implement
The daemon must bundle the following built-in test handlers:

System/OS: hostname, host_ip, cpu (via /proc/stat), ram (via /proc/meminfo).

Network: ping (ICMP or fallback to TCP port 7 echo), tcp_connect (timeout-based socket connection).

Application: service_status (check if a systemd unit is active), url_reachable (HTTP GET/HEAD using libcurl, verifying HTTP 200/3xx).

Database: database_status, database_query (extensible standard SQL checks via MariaDB C connector).

4. Payload Generation (cJSON)
The daemon aggregates all test_result_t items into a strictly formatted JSON payload (Schema Version 2).

Expected JSON Schema:

json
{
  "schema_version": 2,
  "timestamp": 1760000000,
  "agent": {
    "display_name": "myloadbalancer",
    "hostname": "lb-01"
  },
  "results": [
    {
      "type": "cpu",
      "display_name": "CPU Usage",
      "ok": true,
      "value": 0.23,
      "unit": "fraction",
      "detail": "average since last cycle"
      "timestamp:" "1759999991"
    },
    {
      "type": "url_reachable",
      "display_name": "Nextcloud1 Reachability",
      "ok": true,
      "value": 200,
      "unit": "http_status",
      "detail": "reachable"
      "timestamp:" "1759999993"
    },
    {
      "type": "url_reachable",
      "display_name": "Nextcloud2 Reachability",
      "ok": true,
      "value": 200,
      "unit": "http_status",
      "detail": "reachable"
      "timestamp:" "1759999995"
    }
  ]
}
5. Transport & Resilience
Protocol: TCP with a 4-byte big-endian length prefix framing, followed by the JSON string.

Behavior: Connect to master, push payload, close connection. Do not maintain persistent stateful connections.

Resilience: Must include connection timeout handling (non-blocking connects) and retry logic (e.g., 3 attempts with 500ms backoff) to prevent the daemon from blocking or crashing if the master is temporarily down.

Summary of AI Directives:
Use libyaml for configuration parsing.

Use cJSON for payload encoding.

#the tests are:
--- network ---

hostname

ip-address

avg network load

name resolution possible

ping (reachable + latency)

dropped packets / connection tracking table full (conntrack)
--- local services ---

service status

service port

service pid

active connections per service port
--- load balancer (Nginx/HAProxy) ---

backend node state (UP/DOWN transitions per Nextcloud app node)

sticky-session / proxy header propagation check

active client connections vs backend connections

SSL certificate validity (expiration date)

cache hit/miss ratio (if proxy_cache is used)
--- database (MariaDB) ---

from schema:
write latency
read latency
master/slave
rw/ro

GTID

sync-status (timestamped comparison of Master/Slave GTID)

slow query rate

available connections (Threads_connected vs max_connections)

InnoDB buffer pool usage / hit rate
--- Redis ---

sync channel

sync status (comparison of timestamped payload between master and slave)

memory usage vs maxmemory limit

evicted keys rate

connected clients count
--- webserver / PHP (Nginx & PHP-FPM on App Nodes) ---

type (nginx, apache)

port

PHP-FPM active workers vs max_children

PHP-FPM listen queue / rejected connections

OPCache hit rate and memory availability

web response time (synthetic test via localhost loopback)
--- NFS ---

exports exported?

exports mounted?

read/write-test on exports

NFS wait time / iowait metric on client nodes

inode usage on NFS server
--- Nextcloud Application (App Nodes via CLI/API) ---

cron execution timestamp (background jobs running?)

serverinfo API health status (via occ or loopback HTTP)

active users / logged-in sessions

Nextcloud log error rate (counting new "Error" or "Fatal" lines in nextcloud.log)
--- System ---

disk I/O

disk usage

cpu usage

ram usage

system load average (1m, 5m, 15m)

#Build a modular test vtable; DO NOT hardcode host roles (loadbalancer, nextcloud) into the C architecture.

Keep the C code clean, using separate .c files for different test categories (e.g., tests_os.c, tests_net.c, tests_db.c).
