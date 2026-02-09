"""
test_redfish.py - Redfish API Integration Tests

Run: python3 test_redfish.py
Requires: BMC daemon and Redfish server running
"""

import json
import sys
import requests

BASE_URL = "http://localhost:8000"
passed = 0
failed = 0


def test(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  ✓ {name}")
    else:
        failed += 1
        print(f"  ✗ {name} {detail}")


def test_service_root():
    print("\n[TEST] Service Root")
    r = requests.get(f"{BASE_URL}/redfish/v1/")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has @odata.id", "@odata.id" in data)
    test("Has Chassis link", "Chassis" in data)
    test("Has Managers link", "Managers" in data)
    test("RedfishVersion present", "RedfishVersion" in data)


def test_chassis():
    print("\n[TEST] Chassis")
    r = requests.get(f"{BASE_URL}/redfish/v1/Chassis/1")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has ChassisType", data.get("ChassisType") == "RackMount")
    test("Has Status", "Status" in data)
    test("Has Thermal link", "Thermal" in data)


def test_thermal():
    print("\n[TEST] Thermal")
    r = requests.get(f"{BASE_URL}/redfish/v1/Chassis/1/Thermal")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has Temperatures", "Temperatures" in data)
    test("Has Fans", "Fans" in data)

    temps = data["Temperatures"]
    if temps:
        t = temps[0]
        test("Temp has ReadingCelsius", "ReadingCelsius" in t)
        test("Temp has Name", "Name" in t)
        test("Temp reading is number", isinstance(t["ReadingCelsius"], (int, float)))

    oem = data.get("Oem", {}).get("MiniBMC", {})
    test("Has PID info in OEM", "PID" in oem)
    test("Has FanDutyPercent", "FanDutyPercent" in oem)


def test_power():
    print("\n[TEST] Power")
    r = requests.get(f"{BASE_URL}/redfish/v1/Chassis/1/Power")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has Voltages", "Voltages" in data)

    volts = data["Voltages"]
    if volts:
        v = volts[0]
        test("Voltage has ReadingVolts", "ReadingVolts" in v)


def test_sensors():
    print("\n[TEST] Sensor Collection")
    r = requests.get(f"{BASE_URL}/redfish/v1/Chassis/1/Sensors")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has Members", "Members" in data)
    test("Has count", "Members@odata.count" in data)
    test("At least 6 sensors", data.get("Members@odata.count", 0) >= 6)


def test_event_log():
    print("\n[TEST] Event Log (SEL)")
    r = requests.get(
        f"{BASE_URL}/redfish/v1/Managers/1/LogServices/SEL/Entries"
    )
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has Members", "Members" in data)

    entries = data["Members"]
    if entries:
        e = entries[0]
        test("Entry has Message", "Message" in e)
        test("Entry has Severity", "Severity" in e)
        test("Entry has Created", "Created" in e)


def test_secure_boot():
    print("\n[TEST] Secure Boot Verify Action")
    r = requests.post(
        f"{BASE_URL}/redfish/v1/Managers/1/Actions/SecureBoot.Verify"
    )
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has OverallPassed", "OverallPassed" in data)
    test("Has Images", "Images" in data)


def test_dashboard():
    print("\n[TEST] Dashboard")
    r = requests.get(f"{BASE_URL}/dashboard")
    test("Status 200", r.status_code == 200)
    test("Returns HTML", "text/html" in r.headers.get("content-type", ""))
    test("Contains chart", "tempChart" in r.text)


def test_api_state():
    print("\n[TEST] API State (Raw JSON)")
    r = requests.get(f"{BASE_URL}/api/state")
    test("Status 200", r.status_code == 200)
    data = r.json()
    test("Has sensors", "sensors" in data)
    test("Has thermal", "thermal" in data)
    test("Has secure_boot", "secure_boot" in data)


if __name__ == "__main__":
    print("╔══════════════════════════════════════╗")
    print("║    Redfish API Integration Tests     ║")
    print("╚══════════════════════════════════════╝")

    try:
        requests.get(f"{BASE_URL}/redfish/v1/", timeout=2)
    except requests.ConnectionError:
        print("\n✗ Cannot connect to server at", BASE_URL)
        print("  Make sure both bmc_daemon and server.py are running.")
        sys.exit(1)

    test_service_root()
    test_chassis()
    test_thermal()
    test_power()
    test_sensors()
    test_event_log()
    test_secure_boot()
    test_dashboard()
    test_api_state()

    print(f"\n══════════════════════════════════════")
    print(f"Results: {passed} passed, {failed} failed")
    print(f"══════════════════════════════════════")

    sys.exit(1 if failed > 0 else 0)
