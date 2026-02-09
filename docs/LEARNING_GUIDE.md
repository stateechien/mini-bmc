# BMC & Firmware 學習指南 — Axiado PIT

> 從零到面試：需要知道的一切

---

## 學習路徑總覽

```
Week 1: BMC 基礎 + C 語言韌體開發
Week 2: IPMI / Redfish 協議 + Linux Embedded
Week 3: OpenBMC 深入 + Security (RoT/TPM)
Week 4: 動手做 + 準備面試問答
```

---

## Part 1: BMC 是什麼？

### 核心概念

BMC (Baseboard Management Controller) 是伺服器主機板上的獨立微控制器，
即使主機 CPU 關機或當機，BMC 仍然在運行，負責：

1. **遠端管理** — IT 人員可以遠端開關機、看 console、裝 OS
2. **硬體監控** — 讀取溫度/電壓/風扇 sensor
3. **事件記錄** — 記錄硬體錯誤 (SEL - System Event Log)
4. **熱管理** — PID 控制風扇轉速
5. **安全** — Secure Boot、firmware attestation

### BMC 硬體組成

```
伺服器主機板
┌──────────────────────────────────────────┐
│                                          │
│  ┌─────────┐    ┌─────────┐             │
│  │ Host CPU │◄──►│   BMC   │ ← 獨立 SoC │
│  │ (x86)   │KCS │ (ARM)   │             │
│  └─────────┘    └────┬────┘             │
│                      │                   │
│              ┌───────┼───────┐          │
│              │ I2C   │ SPI   │ Network  │
│              │       │       │ (RMCP+)  │
│         ┌────▼──┐ ┌──▼───┐ ┌▼────┐     │
│         │Sensors│ │Flash │ │NIC  │     │
│         │(Temp) │ │(FW)  │ │     │     │
│         └───────┘ └──────┘ └─────┘     │
└──────────────────────────────────────────┘
```

### 關鍵字清單（面試必知）

| 詞彙 | 說明 |
|------|------|
| BMC | Baseboard Management Controller |
| IPMI | Intelligent Platform Management Interface (傳統管理標準) |
| Redfish | DMTF 定義的現代 REST API 管理標準 |
| OpenBMC | Linux Foundation 的開源 BMC firmware stack |
| KCS | Keyboard Controller Style - Host CPU ↔ BMC 溝通介面 |
| SEL | System Event Log - 硬體事件記錄 |
| SOL | Serial over LAN - 遠端 console 重定向 |
| DC-SCM | Data Center Secure Control Module (OCP 標準) |
| RoT | Root of Trust - 安全信任鏈起點 |
| TPM | Trusted Platform Module |
| TCU | Trusted Control/Compute Unit (Axiado 的產品名稱) |

### 推薦閱讀

- [IPMI 2.0 Specification (Intel)](https://www.intel.com/content/www/us/en/products/docs/servers/ipmi/ipmi-second-gen-interface-spec-v2-rev1-1.html)
- [DMTF Redfish API](https://www.dmtf.org/standards/redfish)
- [OpenBMC GitHub](https://github.com/openbmc/openbmc)
- [Axiado TCU Whitepaper](https://axiado.com/)

---

## Part 2: IPMI 協議

### IPMI 命令結構

```
Request:  [NetFn] [Cmd] [Data...]
Response: [Completion Code] [Data...]
```

常用 NetFn:
- 0x06 (App): Get Device ID, Cold/Warm Reset
- 0x04 (Sensor/Event): Get Sensor Reading, Set Sensor Threshold
- 0x0A (Storage): Get/Add SEL Entry, Get FRU Data
- 0x0C (Transport): Get/Set LAN Configuration

### 重要 IPMI 命令

```
# 使用 ipmitool 測試 (面試可能會問)
ipmitool sdr list          # 列出所有 sensor
ipmitool sel elist         # 列出 event log
ipmitool mc info           # BMC 資訊
ipmitool power status      # 主機電源狀態
ipmitool sol activate      # 啟動 Serial over LAN
ipmitool raw 0x06 0x01     # 原始 IPMI: Get Device ID
```

### 推薦閱讀

- `ipmitool` manual page
- IPMI 2.0 spec Chapter 5 (Message Interface) 和 Chapter 35 (Sensor)

---

## Part 3: Redfish API

### 為什麼要學 Redfish？

IPMI 是 1990 年代的 binary 協議，Redfish 是取代它的現代標準：
- REST API (HTTP + JSON)
- 人類可讀、易於 debug
- 支援事件訂閱 (SSE)
- 安全性更好 (HTTPS + Token auth)

### Redfish 資源階層

```
/redfish/v1/                          ServiceRoot
├── /Chassis/1                        硬體機箱
│   ├── /Thermal                      溫度 + 風扇
│   ├── /Power                        電壓 + 功率
│   └── /Sensors                      所有 sensor
├── /Managers/1                       BMC 本身
│   ├── /EthernetInterfaces           BMC 網路設定
│   └── /LogServices/SEL/Entries      事件日誌
├── /Systems/1                        主機系統
│   ├── /Processors                   CPU 資訊
│   └── /Memory                       記憶體資訊
└── /AccountService                   使用者管理
```

### 推薦閱讀

- [Redfish API 入門教學](https://www.dmtf.org/education/redfish)
- OpenBMC bmcweb source code (C++ Redfish 實作)

---

## Part 4: OpenBMC 架構

### 核心元件

```
┌──────────────────────────────────────┐
│           bmcweb (Redfish)           │  ← HTTP/REST
├──────────────────────────────────────┤
│   phosphor-host-ipmid (IPMI/KCS)    │  ← Host IPMI
│   phosphor-net-ipmid (IPMI/RMCP+)   │  ← Network IPMI
├──────────────────────────────────────┤
│         D-Bus (System Bus)           │  ← IPC 機制
├────────────┬─────────────┬───────────┤
│phosphor-   │phosphor-    │phosphor-  │
│hwmon       │pid-control  │logging    │
│(Sensor)    │(Thermal)    │(SEL)      │
├────────────┴─────────────┴───────────┤
│        Linux Kernel (ARM)            │
├──────────────────────────────────────┤
│     U-Boot Bootloader                │
├──────────────────────────────────────┤
│     BMC SoC (AST2600 等)             │
└──────────────────────────────────────┘
```

### 重點學習

1. **D-Bus**: OpenBMC 的核心 IPC 機制，所有 service 透過 D-Bus 通訊
2. **phosphor-pid-control**: 你會很熟悉！就是 PID 熱管理
3. **bmcweb**: Redfish API server，C++ 實作
4. **Yocto/BitBake**: OpenBMC 用 Yocto 建構系統 build firmware image

### 動手體驗

```bash
# Clone OpenBMC 看 source code
git clone https://github.com/openbmc/openbmc.git

# 看 PID control 的設定格式
# openbmc/meta-phosphor/recipes-phosphor/fans/phosphor-pid-control

# 看 Redfish server
# openbmc/meta-phosphor/recipes-phosphor/interfaces/bmcweb
```

---

## Part 5: 安全 — Root of Trust (Axiado 核心業務)

### Secure Boot Chain

```
   ┌────────────┐    驗證     ┌────────────┐    驗證
   │ Hardware   │ ──────────→ │ Bootloader │ ──────────→
   │ Root of    │   SHA/RSA   │            │   SHA/RSA
   │ Trust      │             │            │
   │ (Axiado    │             └────────────┘
   │  TCU)      │
   └────────────┘
                                                         驗證     ┌────────────┐
        ┌────────────┐    驗證     ┌────────────┐ ──────────→ │ Application│
   ──→  │ BMC        │ ──────────→ │ OS Kernel  │   SHA/RSA   │            │
        │ Firmware   │   SHA/RSA   │            │             └────────────┘
        │            │             └────────────┘
        └────────────┘
```

### Axiado 的 TCU 特色

1. **Hardware-Anchored RoT**: 信任根在硬體層，不可被軟體篡改
2. **AI-Driven Detection**: 用 ML 偵測異常 firmware 行為
3. **Runtime Attestation**: 不只開機驗，持續驗證 firmware 完整性
4. **Single-Chip Solution**: 整合安全 + 管理 + 控制在單一 SoC

### 相關標準

- **NIST SP 800-193**: Platform Firmware Resiliency Guidelines
- **OCP Caliptra**: 開源 Root of Trust IP
- **TCG TPM 2.0**: Trusted Platform Module 標準
- **DICE**: Device Identifier Composition Engine

### 推薦閱讀

- Axiado TCU product page 和 whitepapers
- NIST SP 800-193 (PDF)
- OCP Caliptra GitHub

---

## Part 6: Linux Embedded & 硬體介面

### BMC 常用介面

| 介面 | 用途 | Linux Driver |
|------|------|-------------|
| I2C | 讀取溫度/電壓 sensor | `/dev/i2c-*` |
| SPI | 讀寫 Flash (firmware) | `/dev/spi*` |
| UART | Serial console, debug | `/dev/ttyS*` |
| GPIO | LED 控制、reset 按鈕 | `/sys/class/gpio/` |
| PWM | 風扇轉速控制 | `/sys/class/hwmon/` |
| ADC | 類比電壓量測 | `/sys/bus/iio/` |

### I2C 操作範例

```c
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// 讀取 TMP75 溫度 sensor (I2C address 0x48)
int fd = open("/dev/i2c-0", O_RDWR);
ioctl(fd, I2C_SLAVE, 0x48);

uint8_t reg = 0x00;  // Temperature register
write(fd, &reg, 1);

uint8_t buf[2];
read(fd, buf, 2);

// 轉換: TMP75 是 12-bit, 0.0625°C/bit
int16_t raw = (buf[0] << 8) | buf[1];
double temp = (raw >> 4) * 0.0625;
```

### 推薦學習

- Linux I2C subsystem documentation
- Device Tree 基礎 (描述硬體拓撲)
- `i2cdetect`, `i2cget`, `i2cset` 工具

---

## Part 7: 優勢 — 如何在面試中展現

### 獨特優勢

| 經驗 | 對應到 PIT 工作 |
|---------|---------------|
| PID 控制  | BMC 熱管理 PID 控制 |
| MOXA TSN switch 操作 | 硬體設備調試經驗 |
| Python GUI + 控制軟體 | BMC management tool 開發 |
| C 語言 (SnakeWithChatroom) | BMC firmware 開發 |
| 統計分析 (MAE, RMSE, Cpk) | 品質驗證、性能分析 |
| TSMC 實習 | 溫度建模、散熱理解 |

### 面試 Q&A 準備

**Q: 為什麼想做 BMC/firmware?**
> PID 控制，核心就是即時系統管理，
> 這跟 BMC 管理伺服器的理念完全一致。

**Q: 了解 Axiado 的 TCU 嗎?**
> TCU 是 hardware-anchored Root of Trust，整合安全、管理、
> 控制在單一 SoC。它用 AI 做 runtime firmware attestation，
> 比傳統只在 boot time 驗證更安全。我在 mini-BMC 專案中
> 實作了簡化版的 secure boot chain 來理解這個概念。

**Q: 解釋 PID 控制經驗**
> PID 同樣的數學直接可以應用到 BMC 的風扇控制：
> 把 process variable 從「網路延遲」換成「CPU 溫度」，
> control variable 從「GCL time slot」換成「fan duty」。
> 還做了完整的統計分析 (MAE, RMSE, Cpk, S/N ratio)。

**Q: 用什麼 debug 工具?**
> 軟體: GDB, Valgrind, strace, printf debugging
> 硬體: 理解 I2C/SPI protocol analyzer, logic analyzer 的概念
> 網路: Wireshark

---

## 推薦學習資源

### 書籍
- "Linux Device Drivers, 3rd Ed" (free online) - Chapter 10 (I2C)
- "Embedded Linux Primer" - 嵌入式 Linux 入門

### 影片
- YouTube: "What is a BMC" by Supermicro
- YouTube: "OpenBMC Introduction" by IBM
- YouTube: "Redfish API Tutorial" by DMTF

### 動手練習
- 在 Raspberry Pi 上用 I2C 讀溫度 sensor
- 在 QEMU 跑 OpenBMC (模擬 BMC 環境)
- 用 `ipmitool` 連接任何有 BMC 的伺服器

### 學習計畫

| 天 | 主題 | 時間 |
|---|------|-----|
| 1 | 看完這份指南 + 跑 mini-bmc 專案 | 3h |
| 2 | 讀 IPMI spec 重點章節 | 2h |
| 3 | 讀 Redfish API tutorial + 試打 API | 2h |
| 4 | Clone OpenBMC，讀 phosphor-pid-control | 3h |
| 5 | 讀 Axiado TCU whitepaper + 安全標準 | 2h |
| 6 | Linux I2C/SPI 實驗 (RPi 或文檔) | 3h |
| 7 | 模擬面試 Q&A | 2h |
