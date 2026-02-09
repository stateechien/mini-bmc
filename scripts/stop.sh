#!/bin/bash
# stop.sh - Gracefully stop all Mini BMC components

echo "[*] Stopping Mini BMC components..."

if [ -f /tmp/bmc_daemon.pid ]; then
    kill "$(cat /tmp/bmc_daemon.pid)" 2>/dev/null && echo "[✓] BMC daemon stopped"
    rm -f /tmp/bmc_daemon.pid
fi

if [ -f /tmp/bmc_redfish.pid ]; then
    kill "$(cat /tmp/bmc_redfish.pid)" 2>/dev/null && echo "[✓] Redfish server stopped"
    rm -f /tmp/bmc_redfish.pid
fi

# Cleanup any remaining processes
pkill -f bmc_daemon 2>/dev/null
pkill -f "python3 server.py" 2>/dev/null

# Cleanup temp files
rm -f /tmp/bmc_state.json /tmp/bmc_sel.json /tmp/bmc_ipmi.sock
rm -rf /tmp/bmc_fw_images

echo "[✓] All components stopped"
