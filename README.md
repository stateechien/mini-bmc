# Mini BMC Simulator

A simplified Baseboard Management Controller (BMC) simulator demonstrating platform integration concepts including sensor monitoring, IPMI command handling, Redfish REST API, PID-based thermal control, and secure boot verification.

> Built as a portfolio project to showcase understanding of BMC firmware architecture and platform integration for server/data center environments.

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                    Web Dashboard                     │
│              (HTML/JS - Port 8080)                   │
└──────────────────────┬──────────────────────────────┘
                       │ HTTP
┌──────────────────────▼──────────────────────────────┐
│              Redfish API Server                      │
│           (Python FastAPI - Port 8000)               │
│  ┌─────────┐ ┌──────────┐ ┌───────────────────┐    │
│  │ Chassis  │ │ Thermal  │ │  Systems/Managers  │    │
│  │ Sensors  │ │ Control  │ │  Event Log (SEL)   │    │
│  └────┬─────┘ └────┬─────┘ └────────┬──────────┘    │
└───────┼─────────────┼───────────────┼────────────────┘
        │  Shared Memory / Unix Socket │
┌───────▼─────────────▼───────────────▼────────────────┐
│              BMC Firmware Daemon (C)                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐ │
│  │ Sensor   │ │  IPMI    │ │   PID    │ │ Secure  │ │
│  │ Polling  │ │ Handler  │ │ Thermal  │ │  Boot   │ │
│  │ Engine   │ │          │ │ Control  │ │ Verify  │ │
│  └──────────┘ └──────────┘ └──────────┘ └─────────┘ │
│  ┌──────────┐ ┌──────────┐                           │
│  │  Event   │ │  JSON    │                           │
│  │   Log    │ │ State DB │                           │
│  └──────────┘ └──────────┘                           │
└──────────────────────────────────────────────────────┘
```

## Features

- **Sensor Simulation**: Temperature, voltage, fan RPM sensors with configurable noise and drift
- **IPMI Command Processing**: Simplified IPMI Get Sensor Reading, Get SEL Entry, Set Fan Speed
- **Redfish REST API**: DMTF Redfish-compliant endpoints for sensor data, thermal info, event logs
- **PID Thermal Control**: Closed-loop fan speed regulation targeting optimal CPU temperature
- **Secure Boot Simulation**: Firmware image hash verification chain (Root of Trust concept)
- **Web Dashboard**: Real-time visualization of sensor data, thermal trends, and system health
- **System Event Log (SEL)**: Persistent event logging with severity levels and timestamps

## Tech Stack

| Component | Language | Key Libraries |
|-----------|----------|---------------|
| BMC Firmware Daemon | C | POSIX threads, JSON-C, OpenSSL |
| Redfish API Server | Python 3 | FastAPI, uvicorn |
| Web Dashboard | HTML/JS | Chart.js, Fetch API |
| Build System | CMake + Makefile | — |

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential cmake libjson-c-dev libssl-dev python3 python3-pip

# Python dependencies
pip3 install fastapi uvicorn requests
```

### Build & Run

```bash
# 1. Build the C firmware daemon
cd firmware
mkdir -p build && cd build
cmake ..
make
cd ../..

# 2. Start the firmware daemon (background)
./firmware/build/bmc_daemon &

# 3. Start the Redfish API server
cd redfish-api
python3 server.py &
cd ..

# 4. Open the Web Dashboard
# Navigate to http://localhost:8000/dashboard in your browser
```

Or use the convenience script:

```bash
chmod +x scripts/start.sh scripts/stop.sh
./scripts/start.sh    # Start all components
./scripts/stop.sh     # Stop all components
```

## Project Structure

```
mini-bmc/
├── firmware/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── sensor.h          # Sensor data structures & polling
│   │   ├── ipmi.h            # IPMI command definitions
│   │   ├── pid_control.h     # PID thermal controller
│   │   ├── event_log.h       # System Event Log (SEL)
│   │   ├── secure_boot.h     # Secure boot verification
│   │   └── bmc_state.h       # Shared state management
│   └── src/
│       ├── main.c            # Daemon entry point
│       ├── sensor.c          # Sensor simulation & polling
│       ├── ipmi.c            # IPMI command processor
│       ├── pid_control.c     # PID closed-loop controller
│       ├── event_log.c       # SEL implementation
│       ├── secure_boot.c     # Firmware hash verification
│       └── bmc_state.c       # JSON state file I/O
├── redfish-api/
│   ├── server.py             # FastAPI Redfish server
│   └── requirements.txt
├── web-ui/
│   └── dashboard.html        # Single-page dashboard
├── tests/
│   ├── test_pid.c            # PID controller unit tests
│   ├── test_sensor.c         # Sensor module tests
│   ├── test_redfish.py       # Redfish API integration tests
│   └── Makefile
├── scripts/
│   ├── start.sh              # Launch all components
│   └── stop.sh               # Graceful shutdown
├── docs/
│   └── LEARNING_GUIDE.md     # Study guide for BMC/firmware topics
├── .gitignore
├── LICENSE
└── README.md
```

## Key Design Decisions

1. **JSON file as IPC**: The C daemon writes sensor state to a JSON file, which the Python API reads. This is simpler than shared memory for a demo while still demonstrating the concept of firmware ↔ management software communication.

2. **PID Thermal Control**: Adapted from real closed-loop control research (TSN GCL scheduling), demonstrating that the same control theory applies to BMC thermal management.

3. **Modular C Architecture**: Each subsystem (sensor, IPMI, PID, secure boot) is a separate module with clean interfaces, mimicking real BMC firmware organization.

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/redfish/v1/` | Service Root |
| GET | `/redfish/v1/Chassis/1` | Chassis info |
| GET | `/redfish/v1/Chassis/1/Thermal` | Temperatures & fans |
| GET | `/redfish/v1/Chassis/1/Power` | Voltage readings |
| GET | `/redfish/v1/Chassis/1/Sensors` | All sensor readings |
| GET | `/redfish/v1/Managers/1/LogServices/SEL/Entries` | Event log |
| POST | `/redfish/v1/Managers/1/Actions/SecureBoot.Verify` | Trigger secure boot check |
| GET | `/dashboard` | Web UI |

## Learning Resources

See [docs/LEARNING_GUIDE.md](docs/LEARNING_GUIDE.md) for a comprehensive study guide covering BMC architecture, IPMI, Redfish, OpenBMC, and related topics.

## License

MIT License - See [LICENSE](LICENSE) for details.
