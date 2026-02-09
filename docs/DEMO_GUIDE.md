# 本地 Demo 環境設定指南

## 一鍵 Demo

```bash
# 1. 安裝依賴 (只需要一次)
./scripts/setup.sh

# 2. 跑互動式 demo
./scripts/demo.sh
```

---

## 環境需求

### 作業系統
- **Ubuntu 20.04+** (推薦) 或任何 Debian-based Linux
- macOS (需要 Homebrew)
- WSL2 on Windows

### 軟體依賴

| 工具 | 用途 | 安裝方式 |
|------|------|---------|
| gcc / g++ | 編譯 C firmware | `apt install build-essential` |
| cmake | Build system | `apt install cmake` |
| libjson-c-dev | JSON 處理 | `apt install libjson-c-dev` |
| libssl-dev | SHA-256 hash | `apt install libssl-dev` |
| python3 | Redfish API server | 通常已預裝 |
| pip3 | Python 套件管理 | `apt install python3-pip` |
| FastAPI + uvicorn | REST server | `pip3 install fastapi uvicorn` |
| 瀏覽器 | Dashboard 顯示 | Chrome / Firefox |

---

## 各系統安裝指令

### Ubuntu / Debian / WSL2
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libjson-c-dev libssl-dev \
                        python3 python3-pip pkg-config
pip3 install fastapi uvicorn requests
```

### macOS (Homebrew)
```bash
brew install cmake json-c openssl pkg-config python3
pip3 install fastapi uvicorn requests

# macOS 可能需要指定 openssl 路徑
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig"
```

### Arch Linux
```bash
sudo pacman -S base-devel cmake json-c openssl python python-pip
pip3 install fastapi uvicorn requests
```

---

## Demo 展示流程 (手動版)

如果想手動一步步展示而不用 demo.sh：

### Step 1: Build
```bash
cd firmware
mkdir -p build && cd build
cmake ..
make -j$(nproc)
cd ../..
```

### Step 2: 跑 Unit Tests
```bash
cd tests
make test_pid
./test_pid        # 應看到 17 passed, 0 failed
cd ..
```

### Step 3: 啟動 Daemon
```bash
./firmware/build/bmc_daemon &
# 觀察 console 輸出: sensor init, secure boot verification, main loop
```

### Step 4: 查看 Sensor 資料
```bash
# Daemon 每 2 秒寫入 /tmp/bmc_state.json
cat /tmp/bmc_state.json | python3 -m json.tool

# 持續觀察溫度變化
watch -n 2 'python3 -c "
import json
with open(\"/tmp/bmc_state.json\") as f:
    s = json.load(f)
cpu = next(x for x in s[\"sensors\"] if x[\"name\"]==\"CPU_Temp\")
duty = s[\"thermal\"][\"fan_duty_percent\"]
sp = s[\"thermal\"][\"pid\"][\"setpoint\"]
print(f\"CPU: {cpu[\"value\"]:.1f}°C | Fan: {duty:.1f}% | SP: {sp}°C\")
"'
```

### Step 5: 啟動 Redfish API
```bash
cd redfish-api
python3 server.py &
cd ..

# 測試 API
curl http://localhost:8000/redfish/v1/ | python3 -m json.tool
curl http://localhost:8000/redfish/v1/Chassis/1/Thermal | python3 -m json.tool
curl http://localhost:8000/redfish/v1/Managers/1/LogServices/SEL/Entries | python3 -m json.tool
curl -X POST http://localhost:8000/redfish/v1/Managers/1/Actions/SecureBoot.Verify | python3 -m json.tool
```

### Step 6: 開啟 Dashboard
```bash
# 瀏覽器打開:
open http://localhost:8000/dashboard     # macOS
xdg-open http://localhost:8000/dashboard # Linux
```

### 結束
```bash
./scripts/stop.sh
```

---

## 面試

### 時間分配 (10 分鐘 demo)
| 時間 | 內容 | 說什麼 |
|------|------|--------|
| 0-1 min | 打開 README，介紹架構圖 | "這是一個簡化的 BMC simulator..." |
| 1-2 min | `./scripts/demo.sh` 啟動 | "用 C 寫的 daemon，CMake build..." |
| 2-4 min | 看 PID 溫度收斂 | "跟我碩論的 TSN PID 同樣的數學..." |
| 4-5 min | Secure Boot 攻擊模擬 | "模擬 Axiado TCU 的 Root of Trust..." |
| 5-7 min | curl Redfish API | "對齊 DMTF Redfish schema..." |
| 7-9 min | 打開 Dashboard | "即時監控 + Chart.js 趨勢圖..." |
| 9-10 min | 看 source code 重點 | 展示 PID code 和中文學習註解 |

### 展示 code 時重點看哪裡
1. **`firmware/src/pid_control.c`** — PID 核心算法 + anti-windup
2. **`firmware/src/sensor.c`** — 熱模型 (溫度如何受風扇影響)
3. **`firmware/src/secure_boot.c`** — SHA-256 chain of trust
4. **`redfish-api/server.py`** — Redfish endpoint 設計

---

## Troubleshooting

### cmake 找不到 json-c
```bash
# 確認安裝
dpkg -l | grep json-c
# 如果沒有
sudo apt install libjson-c-dev
```

### OpenSSL 錯誤 (macOS)
```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cd firmware/build
cmake -DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR ..
make
```

### Port 8000 被佔用
```bash
# 找出佔用的 process
lsof -i :8000
# 或改 port
cd redfish-api
python3 -c "import uvicorn; from server import app; uvicorn.run(app, port=8080)"
```

### Dashboard 沒資料
確認 bmc_daemon 正在運行且 /tmp/bmc_state.json 存在：
```bash
ps aux | grep bmc_daemon
ls -la /tmp/bmc_state.json
```
