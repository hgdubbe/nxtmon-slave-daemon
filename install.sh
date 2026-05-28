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
    sudo systemctl stop nxtmon-slave.service >/dev/null 2>&1 || true
    sudo systemctl disable nxtmon-slave.service >/dev/null 2>&1 || true
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

# 0. Environment Setup
# If not run inside the repo, clone it into a temp dir so bash -c "$(curl...)" works anywhere
if [ ! -d "src" ] || [ ! -f "src/main.c" ]; then
    echo -e "${YELLOW}Not inside repository. Cloning from GitHub...${NC}"
    TEMP_DIR=$(mktemp -d)
    git clone -q https://github.com/hgdubbe/nxtmon-slave-daemon.git "$TEMP_DIR"
    cd "$TEMP_DIR"
fi

# 1. Dependency Check & Installation
echo -e "${YELLOW}[1/5] Checking System Dependencies...${NC}"

DEPS=("build-essential" "libyaml-dev" "libcurl4-openssl-dev" "libmariadb-dev" "libmariadb-dev-compat" "libhiredis-dev" "libcjson-dev" "libssl-dev")
MISSING_DEPS=()

for pkg in "${DEPS[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_DEPS+=("$pkg")
    fi
done

if [ ${#MISSING_DEPS[@]} -eq 0 ]; then
    echo -e "${GREEN}All required dependencies are already installed!${NC}\n"
else
    echo -e "The following dependencies are missing:"
    for pkg in "${MISSING_DEPS[@]}"; do
        echo -e "  - $pkg"
    done
    echo ""
    read -p "Do you want to install these missing dependencies? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo -e "Updating apt cache silently..."
        sudo apt-get update -qq >/dev/null 2>&1

        echo -e "Installing dependencies:"
        HAS_ERROR=0
        for pkg in "${MISSING_DEPS[@]}"; do
            echo -ne "  [ ] $pkg \r"
            if sudo DEBIAN_FRONTEND=noninteractive apt-get install -yq "$pkg" >apt_error.log 2>&1; then
                echo -e "  [${GREEN}✓${NC}] $pkg"
            else
                echo -e "  [${RED}X${NC}] $pkg"
                echo -e "\n${RED}Failed to install $pkg. Error details:${NC}"
                cat apt_error.log
                HAS_ERROR=1
            fi
        done
        rm -f apt_error.log

        if [ $HAS_ERROR -eq 0 ]; then
            echo -e "${GREEN}\nAll missing dependencies installed successfully!${NC}\n"
        else
            echo -e "${RED}\nSome dependencies failed to install. Please resolve the errors above.${NC}"
            exit 1
        fi
    else
        echo -e "Skipping dependency installation. (Compilation might fail)\n"
    fi
fi

# 2. Compilation
echo -e "${YELLOW}[2/5] Compiling nxtmon-slave...${NC}"
read -p "Do you want to compile the daemon now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Auto-generate Makefile if it wasn't committed to the repo
    if [ ! -f "Makefile" ]; then
        echo -e "Makefile missing. Generating it on the fly..."
        cat > Makefile << 'EOF'
CC = gcc
CFLAGS = -Wall -O2 -Wno-stringop-truncation -Wno-misleading-indentation -Wno-discarded-qualifiers
LDFLAGS = -lyaml -lcurl -lmariadb -lhiredis -lcjson -lssl -lcrypto
SRC_DIR = src
OBJ_DIR = obj
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = nxtmon-slave
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
.PHONY: all clean
EOF
    fi

    make clean >/dev/null 2>&1 || true
    if make >build.log 2>&1; then
        echo -e "${GREEN}Compilation successful!${NC}\n"
    else
        echo -e "${RED}Compilation failed. Build log:${NC}"
        cat build.log
        exit 1
    fi
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
    if [ -f "docs/reference_config.yaml" ]; then
        if [ ! -f "/etc/nxtmon/config.yaml" ]; then
            sudo cp docs/reference_config.yaml /etc/nxtmon/config.yaml
            echo -e "Copied docs/reference_config.yaml to /etc/nxtmon/config.yaml"
        else
            echo -e "Configuration /etc/nxtmon/config.yaml already exists. Skipping overwrite."
        fi
    else
        echo -e "${YELLOW}Warning: docs/reference_config.yaml not found. You will need to manually create /etc/nxtmon/config.yaml${NC}"
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
    sudo systemctl enable nxtmon-slave.service >/dev/null 2>&1
    sudo systemctl restart nxtmon-slave.service
    
    echo -e "${GREEN}systemd service installed and started!${NC}"
    echo -e "Check status with: ${BLUE}sudo systemctl status nxtmon-slave${NC}\n"
else
    echo -e "Skipping systemd setup.\n"
fi

# 5. Finish
echo -e "${YELLOW}[5/5] All Done!${NC}"
echo -e "The daemon will poll and push telemetry every 10 seconds."
echo -e "Make sure to edit ${BLUE}/etc/nxtmon/config.yaml${NC} with your master node details."
echo -e "To uninstall in the future, run:"
echo -e "${BLUE}sudo bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/hgdubbe/nxtmon-slave-daemon/main/install.sh)\" -- --uninstall${NC}"
echo -e "======================================================\n"
