#!/bin/bash
# ============================================
# SkyGuard Installation Script
# ============================================
#
# For interview: "I wrote an idempotent installation script that can be
# run multiple times safely. It creates the service user, sets up logging
# directories, and configures systemd."
#
# Usage:
#   sudo ./install.sh
#

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check for root
if [[ $EUID -ne 0 ]]; then
    log_error "This script must be run as root"
    exit 1
fi

INSTALL_DIR="/usr/local/bin"
LOG_DIR="/var/log/skyguard"
SERVICE_USER="skyguard"

# ============================================
# Create Service User
# ============================================
log_info "Creating service user..."

if id "$SERVICE_USER" &>/dev/null; then
    log_warn "User $SERVICE_USER already exists"
else
    useradd --system --no-create-home --shell /bin/false "$SERVICE_USER"
    log_info "Created user: $SERVICE_USER"
fi

# ============================================
# Create Log Directory
# ============================================
log_info "Creating log directory..."

mkdir -p "$LOG_DIR"
chown "$SERVICE_USER:$SERVICE_USER" "$LOG_DIR"
chmod 750 "$LOG_DIR"

# ============================================
# Install Binary
# ============================================
log_info "Installing skyguard binary..."

if [[ -f "./skyguard" ]]; then
    cp ./skyguard "$INSTALL_DIR/"
    chmod 755 "$INSTALL_DIR/skyguard"
    log_info "Installed: $INSTALL_DIR/skyguard"
else
    log_error "Binary not found. Build first with: cmake --build build"
    exit 1
fi

# ============================================
# Install systemd Service
# ============================================
log_info "Installing systemd service..."

cp ./skyguard.service /etc/systemd/system/
chmod 644 /etc/systemd/system/skyguard.service

# Reload systemd
systemctl daemon-reload
log_info "Service installed: skyguard.service"

# ============================================
# Enable and Start Service
# ============================================
log_info "Enabling service..."
systemctl enable skyguard

log_info "Starting service..."
systemctl start skyguard

# Wait a moment and check status
sleep 2
if systemctl is-active --quiet skyguard; then
    log_info "SkyGuard is running!"
else
    log_error "Service failed to start. Check: journalctl -u skyguard"
    exit 1
fi

# ============================================
# Summary
# ============================================
echo ""
echo "============================================"
echo "  SkyGuard Installation Complete"
echo "============================================"
echo ""
echo "  Service Status: $(systemctl is-active skyguard)"
echo "  Binary:         $INSTALL_DIR/skyguard"
echo "  Logs:           $LOG_DIR/"
echo ""
echo "  Commands:"
echo "    systemctl status skyguard   - Check status"
echo "    journalctl -u skyguard -f   - View logs"
echo "    systemctl restart skyguard  - Restart service"
echo ""
