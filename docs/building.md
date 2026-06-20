# Build & Flash

## Build Firmware

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

Firmware output:

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

## Test

```powershell
pio test -e native_nag
pio test -e native_twai
pio test -e native_log_buffer
py -3 -m unittest test/test_wifi_settings_regression.py
```

## Upload

Adjust the serial port for your machine:

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM14
```

## Erase Then Upload

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t erase --upload-port COM14
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM14
```

## WebUI Regeneration

After editing `include/web/mcp2515_dashboard_ui.src.h`:

```powershell
py -3 scripts/minify_dashboard.py
pio run -e wifi_nag_ESP32_S3_CAN
```
