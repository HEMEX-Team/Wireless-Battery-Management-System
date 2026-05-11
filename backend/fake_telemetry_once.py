"""One-shot fake-master telemetry publisher for OTA testing.

Publishes ONE message that looks like the real master's payload, so the
backend auto-creates a pack with master_pairing_code set. After that, the
real master ESP32 (which is already subscribed to bms/cmd/<code>) will
receive any OTA command we dispatch through /v1/packs/{id}/ota.

Usage (from backend/ with venv active):
    python fake_telemetry_once.py 57CEB0
where 57CEB0 is your master's pairing code from the serial monitor.
"""

import json
import os
import sys
import time

import paho.mqtt.client as mqtt

if len(sys.argv) < 2:
    print("Usage: python fake_telemetry_once.py <MASTER_PAIRING_CODE>")
    sys.exit(1)

MASTER_PAIRING_CODE = sys.argv[1].strip().upper()
SLAVE_PAIRING_CODE = os.getenv("SLAVE_PAIRING_CODE", "FAKE01")

BROKER = os.getenv("MQTT_BROKER", "143.198.121.33")
PORT = int(os.getenv("MQTT_PORT", "1883"))
USER = os.getenv("MQTT_USERNAME", "wbms-backend")
PASS = os.getenv("MQTT_PASSWORD", "mito1234")
TOPIC = "bms/data"

payload = {
    "senderIndex": 1,
    "connectedCells": 3,
    "pairingCode": SLAVE_PAIRING_CODE,
    "masterPairingCode": MASTER_PAIRING_CODE,
    "fwVersion": "0.1.0",
    "v1": 3700, "v2": 3705, "v3": 3695,
    "vStack": 11100,
    "vPack": 11050,
    "current": -120,
    "charge": 1234.5,
    "chargeTime": 60,
    "chipTemp": 30.0,
    "temp1": 28.0,
    "temp2": 28.5,
    "temp3": 28.2,
    "isCharging": False,
    "isDischarging": True,
    "soc": 78.0,
    "message": f"BMS:{SLAVE_PAIRING_CODE}",
}

client = mqtt.Client(
    client_id="fake-master-oneshot",
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
)
if USER:
    client.username_pw_set(USER, PASS)

print(f"Connecting to {BROKER}:{PORT} as {USER!r}...")
client.connect(BROKER, PORT, keepalive=30)
client.loop_start()
time.sleep(1.0)

print(f"Publishing telemetry: slave={SLAVE_PAIRING_CODE} master={MASTER_PAIRING_CODE}")
info = client.publish(TOPIC, json.dumps(payload), qos=1)
info.wait_for_publish(timeout=5)
print(f"  rc={info.rc}, mid={info.mid}")

time.sleep(0.5)
client.loop_stop()
client.disconnect()
print("Done.")
