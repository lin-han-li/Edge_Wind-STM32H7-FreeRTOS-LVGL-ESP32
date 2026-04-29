# ESP32 SPI Coprocessor

ESP-IDF 5.5.4 project for `ESP32-WROOM-32E/UE` acting as a native communication coprocessor for the STM32H750 system.

Current design goals:

- `SPI` is the primary STM32 <-> ESP32 business link.
- `UART0 + EN + GPIO0` remain maintenance and flashing lines.
- Keep cloud API compatibility in phase 1:
  - `POST /api/register`
  - `POST /api/node/heartbeat`
- Accept structured summary/full telemetry from STM32.
- Handle Wi-Fi, HTTP, retries, and server command parsing on ESP32.

Default board assumptions:

- Status LED: `GPIO2`
- SPI slave lines:
  - `SCLK=GPIO18`
  - `MOSI=GPIO23`
  - `MISO=GPIO19`
  - `CS=GPIO5`
  - `RDY/IRQ=GPIO27`

The project is split into:

- `main.c`
  - top-level integration and protocol dispatch
- `spi_link.*`
  - SPI slave transport and mailbox
- `comm_protocol.*`
  - packet format, CRC, message IDs
- `app_config.*`
  - config persistence and defaults
- `wifi_manager.*`
  - Wi-Fi connection management
- `cloud_client.*`
  - register/heartbeat upload worker
- `report_buffer.*`
  - summary/full frame assembly from STM32 packets
- `report_codec.*`
  - JSON serialization and server response parsing

Build flow in ESP-IDF shell:

```bat
cd /d C:\Users\pengjianzhong\Desktop\MY_Project\ESP32\esp32_spi_coprocessor
idf.py set-target esp32
idf.py build
```

First validation targets:

1. Firmware boots and logs protocol readiness.
2. Wi-Fi configuration can be written over the internal protocol.
3. SPI slave transactions complete with `HELLO`, `QUERY_STATUS`, and `PING`.
4. `REGISTER` and `SUMMARY` uploads succeed against the existing server.
