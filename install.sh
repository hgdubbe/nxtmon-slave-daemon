#!/bin/bash
# ==============================================================================
# nxtmon-slave-daemon Installer Script
# ==============================================================================

set -e

# Styling
GREEN='\033[1;32m'
BLUE='\033[1;34m'
YELLOW='\033[1;33m'
RED='\033[1;31m'
NC='\033[0m'

# Check if user wants to uninstall
if [ "$1" == "--uninstall" ]; then
    echo -e "${RED}Uninstalling nxtmon-slave-daemon...${NC}"
    sudo systemctl stop nxtmon-slave.service || true
    sudo systemctl disable nxtmon-slave.service || true
    sudo rm -f /etc/systemd/system/nxtmon-slave.service
    sudo systemctl daemon-reload
    sudo rm -f /usr/local/bin/nxtmon-slave
    echo -e "${YELLOW}Binaries and services removed.${NC}"
    read -p "Do you want to delete the configuration files in /etc/nxtmon? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo rm -rf /etc/nxtmon
        echo -e "${GREEN}Configuration deleted. Uninstallation complete.${NC}"
    else
        echo -e "${GREEN}Configuration kept. Uninstallation complete.${NC}"
    fi
    exit 0
fi

echo -e "${BLUE}======================================================${NC}"
echo -e "${GREEN}      nxtmon-slave-daemon Auto-Installer${NC}"
echo -e "${BLUE}======================================================${NC}\n"

# 1. Dependency Check & Installation
echo -e "${YELLOW}[1/5] Checking System Dependencies...${NC}"
read -p "Do you want to check and install missing apt dependencies? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "Installing build-essential, libyaml-dev, libcurl4-openssl-dev, libmariadb-dev-compat, libhiredis-dev, libcjson-dev, libssl-dev..."
    sudo apt-get update
    sudo apt-get install -y build-essential libyaml-dev libcurl4-openssl-dev libmariadb-dev libmariadb-dev-compat libhiredis-dev libcjson-dev libssl-dev
    echo -e "${GREEN}Dependencies installed successfully!${NC}\n"
else
    echo -e "Skipping dependency installation.\n"
fi

# 2. Compilation
echo -e "${YELLOW}[2/5] Compiling nxtmon-slave...${NC}"
read -p "Do you want to compile the daemon now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ ! -f "Makefile" ]; then
        echo -e "${RED}Error: Makefile not found in the current directory.${NC}"
        exit 1
    fi
    make clean
    make
    if [ ! -f "nxtmon-slave" ]; then
        echo -e "${RED}Compilation failed. Binary 'nxtmon-slave' not generated.${NC}"
        exit 1
    fi
    echo -e "${GREEN}Compilation successful!${NC}\n"
else
    if [ ! -f "nxtmon-slave" ]; then
        echo -e "${RED}Binary 'nxtmon-slave' not found. You must compile it first.${NC}"
        exit 1
    fi
    echo -e "Skipping compilation.\n"
fi

# 3. System Installation
echo -e "${YELLOW}[3/5] Installing binary and configuration...${NC}"
read -p "Do you want to install nxtmon-slave to /usr/local/bin and create /etc/nxtmon? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo mv nxtmon-slave /usr/local/bin/
    sudo chmod +x /usr/local/bin/nxtmon-slave
    
    sudo mkdir -p /etc/nxtmon
    if [ -f "docs/reference.yaml" ]; then
        if [ ! -f "/etc/nxtmon/config.yaml" ]; then
            sudo cp docs/reference.yaml /etc/nxtmon/config.yaml
            echo -e "Copied reference.yaml to /etc/nxtmon/config.yaml"
        else
            echo -e "Configuration /etc/nxtmon/config.yaml already exists. Skipping overwrite."
        fi
    else
        echo -e "${YELLOW}Warning: docs/reference.yaml not found. You will need to manually create /etc/nxtmon/config.yaml${NC}"
    fi
    echo -e "${GREEN}Installation complete!${NC}\n"
else
    echo -e "Skipping system installation. You can run it locally.\n"
fi

# 4. Systemd Service Setup
echo -e "${YELLOW}[4/5] Setting up systemd Service...${NC}"
read -p "Do you want to install and enable the systemd daemon? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    SERVICE_FILE="/etc/systemd/system/nxtmon-slave.service"
    
    sudo bash -c "cat > $SERVICE_FILE" << 'EOF'
[Unit]
Description=nxtmon-slave Telemetry Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/nxtmon-slave /etc/nxtmon/config.yaml
Restart=always
RestartSec=10
User=root

[Install]
WantedBy=multi-user.target
EOF

    sudo systemctl daemon-reload
    sudo systemctl enable nxtmon-slave.service
    sudo systemctl start nxtmon-slave.service
    
    echo -e "${GREEN}systemd service installed and started!${NC}"
    echo -e "Check status with: ${BLUE}sudo systemctl status nxtmon-slave${NC}\n"
else
    echo -e "Skipping systemd setup.\n"
fi

# 5. Finish
echo -e "${YELLOW}[5/5] All Done!${NC}"
echo -e "The daemon will poll and push telemetry every 10 seconds."
echo -e "Make sure to edit ${BLUE}/etc/nxtmon/config.yaml${NC} with your master node details."
echo -e "To uninstall in the future, run: ${BLUE}./install.sh --uninstall${NC}"
echo -e "======================================================\n"
