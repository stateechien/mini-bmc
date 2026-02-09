#!/bin/bash
# start.sh - Launch all Mini BMC components
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "╔══════════════════════════════════════════╗"
echo "║     Starting Mini BMC Simulator          ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Step 1: Build firmware if needed
if [ ! -f "$PROJECT_DIR/firmware/build/bmc_daemon" ]; then
    echo "[*] Building firmware daemon..."
    mkdir -p "$PROJECT_DIR/firmware/build"
    cd "$PROJECT_DIR/firmware/build"
    cmake ..
    make -j$(nproc)
    cd "$PROJECT_DIR"
    echo "[✓] Firmware built successfully"
else
    echo "[✓] Firmware already built"
fi

# Step 2: Start firmware daemon
echo "[*] Starting BMC firmware daemon..."
"$PROJECT_DIR/firmware/build/bmc_daemon" &
BMC_PID=$!
echo "[✓] BMC daemon started (PID: $BMC_PID)"

# Wait for state file to be created
sleep 2

# Step 3: Start Redfish API server
echo "[*] Starting Redfish API server..."
cd "$PROJECT_DIR/redfish-api"
python3 server.py &
API_PID=$!
cd "$PROJECT_DIR"
echo "[✓] Redfish API started (PID: $API_PID)"

# Save PIDs for stop script
echo "$BMC_PID" > /tmp/bmc_daemon.pid
echo "$API_PID" > /tmp/bmc_redfish.pid

sleep 1
echo ""
echo "╔══════════════════════════════════════════╗"
echo "║  All components running!                 ║"
echo "║                                          ║"
echo "║  Dashboard:  http://localhost:8000/dashboard  ║"
echo "║  Redfish:    http://localhost:8000/redfish/v1/ ║"
echo "║                                          ║"
echo "║  Stop with:  ./scripts/stop.sh           ║"
echo "╚══════════════════════════════════════════╝"

# Keep running in foreground
wait
