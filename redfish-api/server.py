"""
Redfish API Server - DMTF Redfish-compliant REST API for BMC management.

【學習重點 - Redfish 協議】

Redfish 是 DMTF 定義的現代伺服器管理 REST API 標準，取代 IPMI over network。
- 基於 HTTP/HTTPS + JSON
- RESTful 設計，支援 CRUD 操作
- OData-based schema（超連結式資源導覽）
- 取代了 IPMI 的 RMCP+ (binary protocol over UDP)

Redfish 資源模型:
  /redfish/v1/                    → ServiceRoot
  /redfish/v1/Chassis/1           → Chassis 資訊
  /redfish/v1/Chassis/1/Thermal   → 溫度 & 風扇
  /redfish/v1/Chassis/1/Power     → 電壓 & 功率
  /redfish/v1/Managers/1          → BMC 管理器資訊
  /redfish/v1/Managers/1/LogServices/SEL → 事件日誌

OpenBMC 用 bmcweb 實作 Redfish server (C++ based)。
這裡用 Python FastAPI 簡化實作，但 API 結構完全對齊 Redfish spec。

面試可提的: "我實作了 Redfish-compliant API endpoints,
理解 DMTF schema 和 OData navigation 的概念。"
"""

import json
import os
import time
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse, FileResponse
from fastapi.staticfiles import StaticFiles

app = FastAPI(
    title="Mini BMC Redfish API",
    description="Simplified DMTF Redfish API for BMC sensor management",
    version="1.0.0",
)

# ── Configuration ──

STATE_FILE = "/tmp/bmc_state.json"
SEL_FILE = "/tmp/bmc_sel.json"
DASHBOARD_PATH = Path(__file__).parent.parent / "web-ui" / "dashboard.html"


def read_bmc_state() -> dict:
    """Read current BMC state from the JSON file written by the C daemon."""
    try:
        with open(STATE_FILE, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {
            "sensors": [],
            "thermal": {"fan_duty_percent": 0, "pid": {}},
            "secure_boot": {"overall_passed": False, "images": []},
        }


def read_sel() -> dict:
    """Read System Event Log from JSON file."""
    try:
        with open(SEL_FILE, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {"entries": [], "count": 0}


# ── Redfish Service Root ──


@app.get("/redfish/v1/")
async def service_root():
    """
    Redfish Service Root - entry point for all Redfish navigation.
    Every Redfish response includes @odata metadata for discoverability.
    """
    return {
        "@odata.id": "/redfish/v1/",
        "@odata.type": "#ServiceRoot.v1_5_0.ServiceRoot",
        "Id": "RootService",
        "Name": "Mini BMC Redfish Service",
        "RedfishVersion": "1.8.0",
        "UUID": "00000000-0000-0000-0000-000000000001",
        "Chassis": {"@odata.id": "/redfish/v1/Chassis"},
        "Managers": {"@odata.id": "/redfish/v1/Managers"},
    }


# ── Chassis Endpoints ──


@app.get("/redfish/v1/Chassis")
async def chassis_collection():
    return {
        "@odata.id": "/redfish/v1/Chassis",
        "@odata.type": "#ChassisCollection.ChassisCollection",
        "Name": "Chassis Collection",
        "Members": [{"@odata.id": "/redfish/v1/Chassis/1"}],
        "Members@odata.count": 1,
    }


@app.get("/redfish/v1/Chassis/1")
async def chassis_info():
    state = read_bmc_state()
    return {
        "@odata.id": "/redfish/v1/Chassis/1",
        "@odata.type": "#Chassis.v1_14_0.Chassis",
        "Id": "1",
        "Name": "Mini BMC Simulated Server",
        "ChassisType": "RackMount",
        "Manufacturer": "MiniProject",
        "Model": "BMC-SIM-1000",
        "SerialNumber": "SIM00001",
        "Status": {
            "State": "Enabled",
            "Health": _get_overall_health(state),
        },
        "Thermal": {"@odata.id": "/redfish/v1/Chassis/1/Thermal"},
        "Power": {"@odata.id": "/redfish/v1/Chassis/1/Power"},
        "Links": {
            "ManagedBy": [{"@odata.id": "/redfish/v1/Managers/1"}],
        },
    }


# ── Thermal (Temperature + Fan) ──


@app.get("/redfish/v1/Chassis/1/Thermal")
async def thermal():
    """
    Redfish Thermal resource - temperatures and fan speeds.
    This is the main endpoint BMC management tools monitor.
    """
    state = read_bmc_state()

    temperatures = []
    fans = []

    for i, sensor in enumerate(state.get("sensors", [])):
        if sensor["type"] == "Temperature":
            temperatures.append({
                "@odata.id": f"/redfish/v1/Chassis/1/Thermal#/Temperatures/{i}",
                "MemberId": str(i),
                "Name": sensor["name"],
                "ReadingCelsius": round(sensor["value"], 1),
                "Status": {
                    "State": "Enabled",
                    "Health": _sensor_health(sensor["status"]),
                },
                "UpperThresholdNonCritical": sensor.get("max_warning"),
                "UpperThresholdCritical": sensor.get("max_critical"),
            })
        elif sensor["type"] == "Fan":
            fans.append({
                "@odata.id": f"/redfish/v1/Chassis/1/Thermal#/Fans/{i}",
                "MemberId": str(i),
                "Name": sensor["name"],
                "Reading": int(sensor["value"]),
                "ReadingUnits": "RPM",
                "Status": {
                    "State": "Enabled",
                    "Health": _sensor_health(sensor["status"]),
                },
            })

    thermal_info = state.get("thermal", {})
    pid_info = thermal_info.get("pid", {})

    return {
        "@odata.id": "/redfish/v1/Chassis/1/Thermal",
        "@odata.type": "#Thermal.v1_7_0.Thermal",
        "Id": "Thermal",
        "Name": "Thermal",
        "Temperatures": temperatures,
        "Fans": fans,
        "Oem": {
            "MiniBMC": {
                "FanDutyPercent": round(thermal_info.get("fan_duty_percent", 0), 1),
                "PID": {
                    "Kp": pid_info.get("kp", 0),
                    "Ki": pid_info.get("ki", 0),
                    "Kd": pid_info.get("kd", 0),
                    "Setpoint": pid_info.get("setpoint", 0),
                    "Output": round(pid_info.get("output", 0), 1),
                },
            }
        },
    }


# ── Power (Voltage Sensors) ──


@app.get("/redfish/v1/Chassis/1/Power")
async def power():
    state = read_bmc_state()

    voltages = []
    for i, sensor in enumerate(state.get("sensors", [])):
        if sensor["type"] == "Voltage":
            voltages.append({
                "@odata.id": f"/redfish/v1/Chassis/1/Power#/Voltages/{i}",
                "MemberId": str(i),
                "Name": sensor["name"],
                "ReadingVolts": round(sensor["value"], 3),
                "Status": {
                    "State": "Enabled",
                    "Health": _sensor_health(sensor["status"]),
                },
                "LowerThresholdNonCritical": sensor.get("min_valid"),
                "UpperThresholdNonCritical": sensor.get("max_warning"),
                "UpperThresholdCritical": sensor.get("max_critical"),
            })

    return {
        "@odata.id": "/redfish/v1/Chassis/1/Power",
        "@odata.type": "#Power.v1_7_0.Power",
        "Id": "Power",
        "Name": "Power",
        "Voltages": voltages,
    }


# ── All Sensors (flat list) ──


@app.get("/redfish/v1/Chassis/1/Sensors")
async def all_sensors():
    state = read_bmc_state()
    members = []
    for i, sensor in enumerate(state.get("sensors", [])):
        members.append({
            "@odata.id": f"/redfish/v1/Chassis/1/Sensors/{i}",
            "Id": str(i),
            "Name": sensor["name"],
            "Type": sensor["type"],
            "Reading": round(sensor["value"], 2),
            "Status": sensor["status"],
            "LastUpdated": sensor.get("last_updated", 0),
        })
    return {
        "@odata.id": "/redfish/v1/Chassis/1/Sensors",
        "Name": "Sensor Collection",
        "Members": members,
        "Members@odata.count": len(members),
    }


# ── Managers (BMC Info) ──


@app.get("/redfish/v1/Managers")
async def managers_collection():
    return {
        "@odata.id": "/redfish/v1/Managers",
        "Name": "Manager Collection",
        "Members": [{"@odata.id": "/redfish/v1/Managers/1"}],
        "Members@odata.count": 1,
    }


@app.get("/redfish/v1/Managers/1")
async def manager_info():
    return {
        "@odata.id": "/redfish/v1/Managers/1",
        "@odata.type": "#Manager.v1_12_0.Manager",
        "Id": "1",
        "Name": "Mini BMC Manager",
        "ManagerType": "BMC",
        "FirmwareVersion": "1.0.0",
        "Status": {"State": "Enabled", "Health": "OK"},
        "LogServices": {
            "@odata.id": "/redfish/v1/Managers/1/LogServices"
        },
        "Actions": {
            "#SecureBoot.Verify": {
                "target": "/redfish/v1/Managers/1/Actions/SecureBoot.Verify",
            }
        },
    }


# ── Event Log (SEL) ──


@app.get("/redfish/v1/Managers/1/LogServices")
async def log_services():
    return {
        "@odata.id": "/redfish/v1/Managers/1/LogServices",
        "Name": "Log Services Collection",
        "Members": [
            {"@odata.id": "/redfish/v1/Managers/1/LogServices/SEL"}
        ],
        "Members@odata.count": 1,
    }


@app.get("/redfish/v1/Managers/1/LogServices/SEL")
async def sel_service():
    sel = read_sel()
    return {
        "@odata.id": "/redfish/v1/Managers/1/LogServices/SEL",
        "@odata.type": "#LogService.v1_2_0.LogService",
        "Id": "SEL",
        "Name": "System Event Log",
        "Entries": {
            "@odata.id": "/redfish/v1/Managers/1/LogServices/SEL/Entries"
        },
        "OverWritePolicy": "WrapsWhenFull",
        "MaxNumberOfRecords": 256,
        "Status": {"State": "Enabled", "Health": "OK"},
    }


@app.get("/redfish/v1/Managers/1/LogServices/SEL/Entries")
async def sel_entries():
    sel = read_sel()
    members = []
    for entry in sel.get("entries", []):
        members.append({
            "@odata.id": f"/redfish/v1/Managers/1/LogServices/SEL/Entries/{entry['id']}",
            "Id": str(entry["id"]),
            "Severity": _map_severity(entry.get("severity", "Info")),
            "Created": _timestamp_to_iso(entry.get("timestamp", 0)),
            "EntryType": "SEL",
            "Message": entry.get("message", ""),
            "MessageArgs": [entry.get("source", "")],
        })
    return {
        "@odata.id": "/redfish/v1/Managers/1/LogServices/SEL/Entries",
        "Name": "SEL Entries",
        "Members": members,
        "Members@odata.count": len(members),
    }


# ── Secure Boot Action ──


@app.post("/redfish/v1/Managers/1/Actions/SecureBoot.Verify")
async def secure_boot_verify():
    """Trigger a secure boot verification and return results."""
    state = read_bmc_state()
    sb = state.get("secure_boot", {})
    return {
        "OverallPassed": sb.get("overall_passed", False),
        "Images": sb.get("images", []),
        "Message": "Secure boot verification complete",
    }


# ── Dashboard ──


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard():
    """Serve the web dashboard."""
    if DASHBOARD_PATH.exists():
        return HTMLResponse(DASHBOARD_PATH.read_text())
    return HTMLResponse("<h1>Dashboard not found</h1><p>Place dashboard.html in web-ui/</p>")


# ── Internal JSON endpoint for dashboard ──


@app.get("/api/state")
async def api_state():
    """Raw BMC state for dashboard consumption."""
    state = read_bmc_state()
    sel = read_sel()
    state["sel"] = sel
    return state


# ── Helper Functions ──


def _sensor_health(status: str) -> str:
    mapping = {"OK": "OK", "Warning": "Warning", "Critical": "Critical"}
    return mapping.get(status, "OK")


def _get_overall_health(state: dict) -> str:
    for sensor in state.get("sensors", []):
        if sensor.get("status") == "Critical":
            return "Critical"
    for sensor in state.get("sensors", []):
        if sensor.get("status") == "Warning":
            return "Warning"
    return "OK"


def _map_severity(severity: str) -> str:
    mapping = {"Info": "OK", "Warning": "Warning", "Critical": "Critical"}
    return mapping.get(severity, "OK")


def _timestamp_to_iso(ts: int) -> str:
    if ts == 0:
        return "1970-01-01T00:00:00Z"
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))


# ── Main ──

if __name__ == "__main__":
    import uvicorn

    print("\n╔══════════════════════════════════════════╗")
    print("║     Mini BMC - Redfish API Server        ║")
    print("║     http://localhost:8000/redfish/v1/     ║")
    print("║     http://localhost:8000/dashboard       ║")
    print("╚══════════════════════════════════════════╝\n")

    uvicorn.run(app, host="0.0.0.0", port=8000)
