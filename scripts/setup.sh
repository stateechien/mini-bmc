#!/bin/bash
# setup.sh - 一鍵安裝所有依賴
set -e

echo "╔══════════════════════════════════════════╗"
echo "║   Mini BMC - Environment Setup           ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Detect OS
if [ -f /etc/debian_version ]; then
    echo "[*] Detected: Debian/Ubuntu"
    sudo apt-get update -qq
    sudo apt-get install -y build-essential cmake libjson-c-dev \
        libssl-dev python3 python3-pip pkg-config
elif [ -f /etc/arch-release ]; then
    echo "[*] Detected: Arch Linux"
    sudo pacman -Sy --noconfirm base-devel cmake json-c openssl python python-pip
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "[*] Detected: macOS"
    brew install cmake json-c openssl pkg-config python3
    echo "export PKG_CONFIG_PATH=\"/opt/homebrew/opt/openssl@3/lib/pkgconfig\"" >> ~/.bashrc
    export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig"
else
    echo "[!] Unknown OS. Please install manually:"
    echo "    build-essential cmake libjson-c-dev libssl-dev python3 python3-pip"
    exit 1
fi

echo ""
echo "[*] Installing Python dependencies..."
pip3 install fastapi uvicorn requests 2>/dev/null || \
pip3 install --user fastapi uvicorn requests 2>/dev/null || \
pip3 install --break-system-packages fastapi uvicorn requests

echo ""
echo "[*] Building firmware..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
mkdir -p "$PROJECT_DIR/firmware/build"
cd "$PROJECT_DIR/firmware/build"
cmake ..
make -j$(nproc)

echo ""
echo "[*] Building tests..."
cd "$PROJECT_DIR/tests"
make test_pid

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║   ✓ Setup complete!                      ║"
echo "║   Run: ./scripts/demo.sh                 ║"
echo "╚══════════════════════════════════════════╝"
