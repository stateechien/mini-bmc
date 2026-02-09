#!/bin/bash
# ═══════════════════════════════════════════════════════
#  Mini BMC Simulator - Interactive Demo Script
#  用途：面試展示 / 本地 demo
#  執行：./scripts/demo.sh
# ═══════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}[*] Cleaning up...${NC}"
    kill $BMC_PID 2>/dev/null || true
    kill $API_PID 2>/dev/null || true
    rm -f /tmp/bmc_state.json /tmp/bmc_sel.json /tmp/bmc_ipmi.sock
    rm -rf /tmp/bmc_fw_images
    echo -e "${GREEN}[✓] Cleanup done${NC}"
}
trap cleanup EXIT

pause() {
    echo ""
    echo -e "${CYAN}  ▶ 按 Enter 繼續...${NC}"
    read -r
}

header() {
    echo ""
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}  $1${NC}"
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════${NC}"
    echo ""
}

# ─────────────────────────────────────────────────────
#  INTRO
# ─────────────────────────────────────────────────────

clear
echo -e "${BOLD}"
echo "╔══════════════════════════════════════════════════╗"
echo "║                                                  ║"
echo "║        Mini BMC Simulator - Live Demo            ║"
echo "║        Baseboard Management Controller           ║"
echo "║                                                  ║"
echo "║  展示: Sensor Monitoring | PID Thermal Control   ║"
echo "║        IPMI Commands | Redfish API               ║"
echo "║        Secure Boot | Web Dashboard               ║"
echo "║                                                  ║"
echo "╚══════════════════════════════════════════════════╝"
echo -e "${NC}"
echo ""
echo -e "本 demo 將依序展示 Mini BMC 的 ${BOLD}6 大功能模組${NC}："
echo ""
echo -e "  ${GREEN}1.${NC} Build & 啟動 — 編譯 C 韌體 + 啟動 daemon"
echo -e "  ${GREEN}2.${NC} Sensor 監控 — 即時讀取溫度/電壓/風扇"
echo -e "  ${GREEN}3.${NC} PID 溫控 — 觀察 closed-loop 風扇自動調速"
echo -e "  ${GREEN}4.${NC} Secure Boot — 驗證 firmware 完整性 + 模擬攻擊"
echo -e "  ${GREEN}5.${NC} Redfish API — 測試 REST API endpoints"
echo -e "  ${GREEN}6.${NC} Web Dashboard — 瀏覽器即時監控介面"
pause

# ─────────────────────────────────────────────────────
#  STEP 1: BUILD
# ─────────────────────────────────────────────────────

header "Step 1: Build C Firmware Daemon"

echo -e "${YELLOW}[說明]${NC} BMC firmware 用 C 語言開發，透過 CMake 建置"
echo -e "       包含 7 個模組: main, sensor, pid_control, ipmi,"
echo -e "       event_log, secure_boot, bmc_state"
echo ""

if [ ! -f "$PROJECT_DIR/firmware/build/bmc_daemon" ]; then
    echo -e "${BLUE}[*] 建置中...${NC}"
    mkdir -p "$PROJECT_DIR/firmware/build"
    cd "$PROJECT_DIR/firmware/build"
    cmake .. 2>&1 | grep -E "(Found|Configuring|Generating)"
    make -j$(nproc) 2>&1 | grep -E "(\[|Linking)"
    cd "$PROJECT_DIR"
else
    echo -e "${GREEN}[✓] Firmware 已建置完成${NC}"
fi

echo ""
echo -e "${GREEN}[✓] Build 成功！${NC} 產出: firmware/build/bmc_daemon"
pause

# ─────────────────────────────────────────────────────
#  STEP 1b: Run Unit Tests
# ─────────────────────────────────────────────────────

header "Step 1b: PID Controller Unit Tests"

echo -e "${YELLOW}[說明]${NC} 在啟動 daemon 前，先跑 PID 模組的 unit test"
echo -e "       驗證 PID 初始化、收斂性、output clamping 等"
echo ""

cd "$PROJECT_DIR/tests"
make test_pid 2>/dev/null
./test_pid
cd "$PROJECT_DIR"
pause

# ─────────────────────────────────────────────────────
#  STEP 2: START DAEMON + SENSOR MONITORING
# ─────────────────────────────────────────────────────

header "Step 2: 啟動 BMC Daemon & Sensor 監控"

echo -e "${YELLOW}[說明]${NC} BMC daemon 啟動後會："
echo -e "  1. 初始化 8 個 sensor (3 溫度 + 3 電壓 + 2 風扇)"
echo -e "  2. 執行 Secure Boot 驗證鏈"
echo -e "  3. 啟動 IPMI listener (Unix Socket)"
echo -e "  4. 進入主迴圈: sensor polling → PID 計算 → 狀態輸出"
echo ""
echo -e "${BLUE}[*] 啟動 daemon (背景執行)...${NC}"

"$PROJECT_DIR/firmware/build/bmc_daemon" > /tmp/bmc_demo_log.txt 2>&1 &
BMC_PID=$!
sleep 3

echo -e "${GREEN}[✓] BMC Daemon 已啟動 (PID: $BMC_PID)${NC}"
echo ""

# Show sensor data from JSON
echo -e "${BOLD}── 即時 Sensor 讀數 ──${NC}"
echo ""
python3 -c "
import json
with open('/tmp/bmc_state.json') as f:
    state = json.load(f)

print('  ┌──────────────┬───────────────┬──────────┐')
print('  │ Sensor       │ Value         │ Status   │')
print('  ├──────────────┼───────────────┼──────────┤')
for s in state['sensors']:
    name = s['name'].ljust(12)
    if s['type'] == 'Temperature':
        val = f\"{s['value']:.1f} °C\".rjust(13)
    elif s['type'] == 'Voltage':
        val = f\"{s['value']:.3f} V\".rjust(13)
    else:
        val = f\"{s['value']:.0f} RPM\".rjust(13)
    status = s['status'].ljust(8)
    color = '\033[32m' if s['status'] == 'OK' else '\033[33m' if s['status'] == 'Warning' else '\033[31m'
    print(f'  │ {name} │ {val} │ {color}{status}\033[0m │')
print('  └──────────────┴───────────────┴──────────┘')
"
pause

# ─────────────────────────────────────────────────────
#  STEP 3: PID THERMAL CONTROL
# ─────────────────────────────────────────────────────

header "Step 3: PID Closed-Loop 溫度控制"

echo -e "${YELLOW}[說明]${NC} 觀察 PID 控制器如何自動調整風扇轉速："
echo -e "  - Setpoint (目標溫度) = 65°C"
echo -e "  - Kp=3.0, Ki=0.1, Kd=1.5"
echo -e "  - 溫度高 → PID 增加 fan duty → 風扇加速 → 溫度降下來"
echo ""
echo -e "${BOLD}── PID 狀態追蹤 (每 2 秒更新，共 5 次) ──${NC}"
echo ""

echo "  ┌───────┬──────────┬───────────┬──────────┬───────────┐"
echo "  │  #    │ CPU Temp │ Setpoint  │ Fan Duty │ PID Out   │"
echo "  ├───────┼──────────┼───────────┼──────────┼───────────┤"

for i in 1 2 3 4 5; do
    sleep 2
    python3 -c "
import json
with open('/tmp/bmc_state.json') as f:
    state = json.load(f)
cpu = next((s for s in state['sensors'] if s['name'] == 'CPU_Temp'), None)
thermal = state.get('thermal', {})
pid = thermal.get('pid', {})
temp = cpu['value'] if cpu else 0
sp = pid.get('setpoint', 65)
duty = thermal.get('fan_duty_percent', 0)
output = pid.get('output', 0)

# Color based on how close to setpoint
diff = abs(temp - sp)
color = '\033[32m' if diff < 3 else '\033[33m' if diff < 8 else '\033[31m'

print(f'  │  ${i}/5  │ {color}{temp:7.1f}°C\033[0m │  {sp:6.1f}°C  │  {duty:6.1f}%  │  {output:6.1f}%   │')
"
done

echo "  └───────┴──────────┴───────────┴──────────┴───────────┘"
echo ""
echo -e "${CYAN}[觀察]${NC} CPU 溫度逐漸趨近 setpoint (65°C)，"
echo -e "       fan duty 會相應調整 — 這就是 closed-loop PID 控制！"
echo ""
echo -e "${YELLOW}[面試可提]${NC}"
echo -e "  \"這跟我 TSN 碩論的 PID 完全相同的數學："
echo -e "   TSN: PV=延遲, CV=GCL slot → BMC: PV=溫度, CV=fan duty\""
pause

# ─────────────────────────────────────────────────────
#  STEP 4: SECURE BOOT VERIFICATION
# ─────────────────────────────────────────────────────

header "Step 4: Secure Boot 驗證鏈"

echo -e "${YELLOW}[說明]${NC} 模擬 Root of Trust 的 chain-of-trust 驗證："
echo -e "  Hardware RoT → Bootloader → BMC Firmware → App → Config"
echo -e "  每一層用 SHA-256 hash 驗證下一層"
echo ""

# Show current secure boot status
echo -e "${BOLD}── 正常狀態: 所有 firmware 驗證通過 ──${NC}"
echo ""
python3 -c "
import json
with open('/tmp/bmc_state.json') as f:
    state = json.load(f)
sb = state.get('secure_boot', {})
for img in sb.get('images', []):
    icon = '\033[32m✓ PASS\033[0m' if img['passed'] else '\033[31m✗ FAIL\033[0m'
    print(f'  {icon}  {img[\"name\"].ljust(15)} hash: {img[\"expected_hash\"][:24]}...')
overall = sb.get('overall_passed', False)
icon = '\033[32m🔒 SECURE\033[0m' if overall else '\033[31m⚠ COMPROMISED\033[0m'
print(f'\n  Overall: {icon}')
"

echo ""
echo -e "${RED}[模擬攻擊]${NC} 現在篡改 bmc_firmware image..."
echo ""

# Tamper with firmware
if [ -f /tmp/bmc_fw_images/bmc_firmware.bin ]; then
    # Write a byte to corrupt the file
    printf '\xff' | dd of=/tmp/bmc_fw_images/bmc_firmware.bin bs=1 count=1 conv=notrunc 2>/dev/null
    echo -e "  ${RED}⚡ bmc_firmware.bin 已被竄改 (第 1 byte 被修改)${NC}"
    echo ""

    # Verify again via API (we'll check the hash manually)
    echo -e "${BOLD}── 竄改後: 重新驗證 ──${NC}"
    echo ""
    python3 -c "
import hashlib, json

images = [
    ('bootloader',  42),
    ('bmc_firmware', 43),
    ('application',  44),
    ('config_data',  45),
]

with open('/tmp/bmc_state.json') as f:
    state = json.load(f)
sb_images = state.get('secure_boot', {}).get('images', [])

for name, seed in images:
    path = f'/tmp/bmc_fw_images/{name}.bin'
    try:
        with open(path, 'rb') as f:
            actual = hashlib.sha256(f.read()).hexdigest()
        expected = next((img['expected_hash'] for img in sb_images if img['name'] == name), '')
        match = (actual == expected)
        icon = '\033[32m✓ PASS\033[0m' if match else '\033[31m✗ FAIL - HASH MISMATCH!\033[0m'
        print(f'  {icon}  {name}')
        if not match:
            print(f'       Expected: {expected[:32]}...')
            print(f'       Actual:   {actual[:32]}...')
            print(f'       \033[31m→ Chain of trust BROKEN here, 後續不再驗證\033[0m')
            break
    except FileNotFoundError:
        print(f'  \033[31m✗ {name}: FILE NOT FOUND\033[0m')
"
    echo ""

    # Restore
    echo -e "${GREEN}[修復]${NC} 還原 firmware image..."
    python3 -c "
import random
random.seed(43)
data = bytes([random.randint(0,255) for _ in range(4096)])
with open('/tmp/bmc_fw_images/bmc_firmware.bin', 'wb') as f:
    f.write(data)
print('  ✓ bmc_firmware.bin 已還原')
"
else
    echo -e "  ${YELLOW}(Firmware images 不存在，跳過攻擊模擬)${NC}"
fi

echo ""
echo -e "${YELLOW}[面試可提]${NC}"
echo -e "  \"Axiado 的 TCU 做的就是 hardware-anchored Root of Trust，"
echo -e "   我理解 chain-of-trust 概念，並實作了 SHA-256 驗證鏈。"
echo -e "   真實場景會用 RSA/ECDSA 簽章，且 RoT 在硬體層不可篡改。\""
pause

# ─────────────────────────────────────────────────────
#  STEP 5: REDFISH API
# ─────────────────────────────────────────────────────

header "Step 5: Redfish REST API"

echo -e "${YELLOW}[說明]${NC} Redfish 是 DMTF 定義的現代伺服器管理 REST API 標準"
echo -e "       取代 IPMI over network，使用 HTTP + JSON"
echo ""
echo -e "${BLUE}[*] 啟動 Redfish API Server (FastAPI)...${NC}"

cd "$PROJECT_DIR/redfish-api"
python3 server.py > /tmp/bmc_api_log.txt 2>&1 &
API_PID=$!
cd "$PROJECT_DIR"
sleep 2

echo -e "${GREEN}[✓] Redfish Server 啟動 (Port 8000)${NC}"
echo ""

# Test endpoints
echo -e "${BOLD}── API Endpoint 測試 ──${NC}"
echo ""

echo -e "${CYAN}GET /redfish/v1/${NC} (Service Root)"
curl -s http://localhost:8000/redfish/v1/ | python3 -m json.tool 2>/dev/null | head -12
echo "  ..."
echo ""

echo -e "${CYAN}GET /redfish/v1/Chassis/1/Thermal${NC} (溫度 & 風扇)"
curl -s http://localhost:8000/redfish/v1/Chassis/1/Thermal | python3 -c "
import json, sys
d = json.load(sys.stdin)
print('  Temperatures:')
for t in d.get('Temperatures', []):
    print(f\"    {t['Name']}: {t['ReadingCelsius']}°C ({t['Status']['Health']})\")
print('  Fans:')
for f in d.get('Fans', []):
    print(f\"    {f['Name']}: {f['Reading']} RPM\")
oem = d.get('Oem', {}).get('MiniBMC', {})
print(f\"  Fan Duty: {oem.get('FanDutyPercent', 0):.1f}%\")
pid = oem.get('PID', {})
print(f\"  PID: Kp={pid.get('kp',0)} Ki={pid.get('ki',0)} Kd={pid.get('kd',0)} SP={pid.get('setpoint',0)}°C\")
" 2>/dev/null
echo ""

echo -e "${CYAN}GET /redfish/v1/Chassis/1/Power${NC} (電壓)"
curl -s http://localhost:8000/redfish/v1/Chassis/1/Power | python3 -c "
import json, sys
d = json.load(sys.stdin)
for v in d.get('Voltages', []):
    print(f\"    {v['Name']}: {v['ReadingVolts']}V ({v['Status']['Health']})\")
" 2>/dev/null
echo ""

echo -e "${CYAN}GET /redfish/v1/Managers/1/LogServices/SEL/Entries${NC} (最近 5 筆 Event Log)"
curl -s http://localhost:8000/redfish/v1/Managers/1/LogServices/SEL/Entries | python3 -c "
import json, sys
d = json.load(sys.stdin)
for e in d.get('Members', [])[-5:]:
    sev = e.get('Severity', 'OK')
    color = '\033[32m' if sev == 'OK' else '\033[33m' if sev == 'Warning' else '\033[31m'
    print(f\"    {color}[{sev}]\033[0m {e.get('Message', '')}\")
" 2>/dev/null
echo ""

echo -e "${CYAN}POST /redfish/v1/Managers/1/Actions/SecureBoot.Verify${NC}"
curl -s -X POST http://localhost:8000/redfish/v1/Managers/1/Actions/SecureBoot.Verify | python3 -c "
import json, sys
d = json.load(sys.stdin)
status = '\033[32mPASSED\033[0m' if d.get('OverallPassed') else '\033[31mFAILED\033[0m'
print(f'    Secure Boot: {status}')
" 2>/dev/null

pause

# ─────────────────────────────────────────────────────
#  STEP 6: WEB DASHBOARD
# ─────────────────────────────────────────────────────

header "Step 6: Web Dashboard"

echo -e "${YELLOW}[說明]${NC} 即時 Web UI 顯示所有 BMC 監控資訊"
echo -e "       使用 Chart.js 繪製溫度趨勢圖"
echo ""
echo -e "${BOLD}${GREEN}  ┌─────────────────────────────────────────────┐"
echo -e "  │                                             │"
echo -e "  │   🌐  http://localhost:8000/dashboard       │"
echo -e "  │                                             │"
echo -e "  │   用瀏覽器打開上面的網址即可看到：          │"
echo -e "  │                                             │"
echo -e "  │   • 溫度/電壓/風扇即時讀數                  │"
echo -e "  │   • PID 控制參數 (Kp, Ki, Kd, Setpoint)    │"
echo -e "  │   • CPU 溫度趨勢圖 (自動更新)              │"
echo -e "  │   • Secure Boot 驗證鏈狀態                  │"
echo -e "  │   • System Event Log 即時滾動               │"
echo -e "  │                                             │"
echo -e "  └─────────────────────────────────────────────┘${NC}"
echo ""
echo -e "  Dashboard 每 2 秒自動刷新，可以看到溫度漸漸收斂到 setpoint"
pause

# ─────────────────────────────────────────────────────
#  SUMMARY
# ─────────────────────────────────────────────────────

header "Demo 完成 — 專案總結"

echo -e "${BOLD}技術棧：${NC}"
echo -e "  • C (韌體核心)：sensor polling, IPMI, PID control, secure boot"
echo -e "  • Python (管理層)：FastAPI Redfish server"
echo -e "  • HTML/JS (前端)：即時 dashboard with Chart.js"
echo ""

echo -e "${BOLD}展示了什麼能力：${NC}"
echo -e "  ${GREEN}✓${NC} 嵌入式 C 開發 — 多模組、多執行緒 daemon"
echo -e "  ${GREEN}✓${NC} 控制理論 — PID closed-loop thermal management"
echo -e "  ${GREEN}✓${NC} 通訊協議 — IPMI + Redfish REST API"
echo -e "  ${GREEN}✓${NC} 安全概念 — Root of Trust, Secure Boot chain"
echo -e "  ${GREEN}✓${NC} 系統整合 — firmware ↔ API ↔ dashboard 全棧"
echo -e "  ${GREEN}✓${NC} 軟體工程 — unit tests, CMake build, documentation"
echo ""

echo -e "${BOLD}跟 Axiado PIT 工作的關聯：${NC}"
echo -e "  • PIT = Platform Integration Team → 這個專案就是做平台整合"
echo -e "  • BMC firmware 開發 → sensor, IPMI, thermal control"
echo -e "  • Axiado TCU = Root of Trust → secure boot verification"
echo -e "  • PID 控制 → 直接遷移自 TSN 碩論經驗"
echo ""

echo -e "${YELLOW}════════════════════════════════════════════${NC}"
echo -e "${YELLOW}  Dashboard 仍在運行中${NC}"
echo -e "${YELLOW}  瀏覽器打開 http://localhost:8000/dashboard${NC}"
echo -e "${YELLOW}  按 Ctrl+C 結束 demo${NC}"
echo -e "${YELLOW}════════════════════════════════════════════${NC}"
echo ""

# Keep running so user can browse dashboard
wait $BMC_PID 2>/dev/null
