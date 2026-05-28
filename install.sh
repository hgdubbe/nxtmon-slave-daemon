#!/bin/bash
# nxtmon-slave-daemon install script

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root"
  exit
fi

echo "======================================"
echo "Installing nxtmon-slave-daemon"
echo "======================================"

# Determine the directory where the install script is located
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Create directories
echo "[1/5] Creating directories..."
mkdir -p /etc/nxtmon
mkdir -p /var/log/nxtmon
mkdir -p /usr/local/bin

# Handle Configuration File
echo "[2/5] Setting up configuration..."
DEFAULT_CONFIG="$REPO_DIR/default.yaml"

if [ ! -f "$DEFAULT_CONFIG" ]; then
    echo "ERROR: default.yaml not found in $REPO_DIR!"
    echo "Please ensure you run this script from the repository root."
    exit 1
fi

if [ -f "/etc/nxtmon/config.yaml" ]; then
    echo "Existing configuration found. Creating backup at /etc/nxtmon/config.yaml.bak"
    cp "/etc/nxtmon/config.yaml" "/etc/nxtmon/config.yaml.bak"
fi

echo "Copying default.yaml to /etc/nxtmon/config.yaml..."
cp "$DEFAULT_CONFIG" "/etc/nxtmon/config.yaml"

# Compile and install binary
echo "[3/5] Compiling daemon..."
if ! command -v cmake &> /dev/null; then
    echo "Installing build dependencies (cmake, gcc, libyaml-dev)..."
    apt-get update && apt-get install -y cmake gcc build-essential libyaml-dev
fi

# We build from the repo dir
cd "$REPO_DIR" || exit 1
cmake -B build
cmake --build build --parallel

echo "Installing binary to /usr/local/bin/nxtmond-slave..."
cp build/nxtmond-slave /usr/local/bin/nxtmond-slave
chmod +x /usr/local/bin/nxtmond-slave

# Systemd Service
echo "[4/5] Installing systemd service..."
cat << 'EOF' > /etc/systemd/system/nxtmond-slave.service
[Unit]
Description=nxtmon Slave Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/nxtmond-slave -c /etc/nxtmon/config.yaml
Restart=on-failure
RestartSec=5
User=root

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload

echo "[5/5] Service Setup"
read -p "Do you want to enable and start the service now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    systemctl enable nxtmond-slave.service
    systemctl start nxtmond-slave.service
    echo "Service started."
    systemctl status nxtmond-slave.service --no-pager | head -n 5
else
    echo "Service installed but not started."
    echo "You can start it later with: systemctl start nxtmond-slave.service"
fi

echo "======================================"
echo "Installation complete!"
echo "Config file: /etc/nxtmon/config.yaml"
echo "Log directory: /var/log/nxtmon/"
echo "======================================"
