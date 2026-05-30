# Master OTA — manual test recipe (V1)

End-to-end walkthrough: build the master at v0.1.0, upload a v0.1.1 image,
trigger the OTA, watch the master reflash itself, confirm the new version
shows up in the next MQTT telemetry message.

Out of scope here: slave OTA, frontend, signing keys, deployment-status
table. Those land in M2–M4.

## Prerequisites

- Master ESP32 flashed and connected to WiFi (it must reach the broker)
- Backend running (`uvicorn app.main:app --reload --port 8000`)
- MQTT broker reachable by both backend and master (the broker in
  `pio-Wireless-Battery-Managemnt-System/src/shared/config.h`)
- A backend account — `curl -X POST http://localhost:8000/v1/auth/register`
  then `/v1/auth/login` to get a JWT
- `OTA_URL_SECRET` set in `backend/.env` to anything non-default
- `FIRMWARE_DIR` defaults to `./firmware_artifacts/` (relative to wherever
  uvicorn was launched)

## 1. Build + flash master at v0.1.0

```powershell
cd pio-Wireless-Battery-Managemnt-System
pio run -e master -t upload --upload-port COM5   # adjust COM port
pio device monitor -e master --port COM5
```

Expect serial output:

```
[System] FW v0.1.0, masterPairingCode=ABCDEF
[System] MQTT command topic: bms/cmd/ABCDEF
[MQTT] ✅ Connected!
[MQTT] 🔔 Subscribed to bms/cmd/ABCDEF
[OTA] ✅ Marked running image valid, rollback cancelled
```

The `masterPairingCode` is the last 3 bytes of the STA MAC — capture it for
step 4.

## 2. Confirm telemetry reaches the backend

In another terminal:

```powershell
curl -H "Authorization: Bearer $TOKEN" http://localhost:8000/v1/packs
```

Each pack should now have `master_firmware_version: "0.1.0"` and
`master_pairing_code: "ABCDEF"`. If `master_pairing_code` is null, the
pack hasn't received telemetry yet — wait ~5 seconds.

## 3. Build the v0.1.1 image

Edit `pio-Wireless-Battery-Managemnt-System/src/shared/config.h`:

```c
const char *const FW_VERSION = "0.1.1";
```

Then build (don't flash):

```powershell
pio run -e master
```

Artifact: `.pio/build/master/firmware.bin`.

## 4. Upload it to the backend

```powershell
$TOKEN = "<jwt from /v1/auth/login>"
curl -X POST http://localhost:8000/v1/firmware `
  -H "Authorization: Bearer $TOKEN" `
  -F "version=0.1.1" `
  -F "file=@.pio/build/master/firmware.bin"
```

Response carries the new `FirmwareImage.id` — save it.

List to verify:

```powershell
curl -H "Authorization: Bearer $TOKEN" http://localhost:8000/v1/firmware
```

## 5. Dispatch the OTA

Find the `pack_id` from `GET /v1/packs`, then:

```powershell
curl -X POST "http://localhost:8000/v1/packs/<pack_id>/ota" `
  -H "Authorization: Bearer $TOKEN" `
  -H "Content-Type: application/json" `
  -d '{"firmware_image_id": <image_id>}'
```

Response echoes the topic + payload that was published.

## 6. Watch the master upgrade

Serial output on the master should show:

```
[MQTT] 📨 Command on bms/cmd/ABCDEF: {"op":"ota","url":"https://.../v1/firmware/3/blob?token=...","version":"0.1.1","sha256":"..."}
[OTA] Starting upgrade to v0.1.1
[OTA]   URL:    https://.../v1/firmware/3/blob?token=...
[OTA]   SHA256: <hex>
[OTA] ✅ Image installed, rebooting...
```

Then on next boot:

```
[System] FW v0.1.1, masterPairingCode=ABCDEF
```

## 7. Confirm version flipped in telemetry

```powershell
curl -H "Authorization: Bearer $TOKEN" http://localhost:8000/v1/packs
```

`master_firmware_version` should now read `"0.1.1"`.

## Refusal paths to verify

- Same version: re-dispatch image v0.1.0 while master is on v0.1.0 → master
  logs `[OTA] Refused: already on v0.1.0`
- Low SoC: discharge the test pack below 30% → master logs
  `[OTA] Refused: Sender 1 SoC=...% < 30.0%`
- Fault proxy: bring a cell outside [2.5, 4.25] V (or temp >60 C) → master
  logs `[OTA] Refused: Sender 1 has active protection fault`
- Expired token: wait >5 min after dispatch with the master offline, then
  bring it online → master will get the URL but the blob fetch returns 403

## Notes

- HTTPS is required because the master uses `esp_https_ota` with the
  built-in CA bundle. If the backend is plain HTTP behind a reverse proxy
  in dev, expose it over HTTPS (mkcert + nginx, or ngrok)
- The master blocks its main loop during OTA — ESP-NOW telemetry is paused
  for ~30 s. Expect a gap in the readings table around the upgrade
- After a successful OTA, the new image starts in `PENDING_VERIFY` and is
  marked valid once WiFi + MQTT are both up. If the new image can't reach
  the broker, the bootloader rolls back to the previous slot automatically
  on the next boot.
