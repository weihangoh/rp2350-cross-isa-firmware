#!/usr/bin/env bash
set -euo pipefail

echo "[*] Checking Debug Probe visibility in WSL..."
lsusb | grep -Ei 'Raspberry|CMSIS|DAP|Debug|Picoprobe' || {
    echo "[!] Debug Probe not visible in WSL."
    echo "    Run connect-debug-probe-wsl.ps1 from Windows PowerShell first."
    exit 1
}

echo "[*] Checking OpenOCD..."
command -v openocd >/dev/null || {
    echo "[!] openocd not found."
    echo "    Install/use the Raspberry Pi Pico OpenOCD build with RP2350 support."
    exit 1
}

echo "[*] Checking for rp2350.cfg..."
if ! find /usr /opt "$HOME" -name rp2350.cfg 2>/dev/null | head -1 | grep -q .; then
    echo "[!] rp2350.cfg not found in common locations."
    echo "    Your OpenOCD may not support RP2350."
    exit 1
fi

echo "[*] Starting OpenOCD SWD test..."
sudo openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2350.cfg \
  -c "adapter speed 1000" \
  -c "init" \
  -c "targets" \
  -c "halt" \
  -c "resume" \
  -c "shutdown"

echo "[*] SWD connection test complete."
