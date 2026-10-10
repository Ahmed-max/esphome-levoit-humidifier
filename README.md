# ESPHome Levoit Humidifier

[![Compile](https://github.com/Ahmed-max/esphome-levoit-humidifier/actions/workflows/compile.yml/badge.svg)](https://github.com/Ahmed-max/esphome-levoit-humidifier/actions/workflows/compile.yml)

Local, cloud-free control of Levoit smart humidifiers with ESPHome. The ESP32
module that ships inside the humidifier is reflashed with ESPHome and talks to the
humidifier's own controller over UART, so no extra hardware is needed.

| Model | Code | Status |
|---|---|---|
| Dual 200S | LUH-D301S | ✅ tested (MCU firmware 2.0.02) |

The UART protocol is documented in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Usage

The quickest route is the ready-made package, which brings in the board settings,
UART and every entity:

```yaml
esphome:
  name: levoit-dual200s
  friendly_name: Levoit Dual 200S

packages:
  levoit: github://Ahmed-max/esphome-levoit-humidifier/packages/dual200s.yaml@main

logger:
api:
  encryption:
    key: !secret levoit_api_key
ota:
  - platform: esphome
    password: !secret levoit_ota_password
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

To pick your own entities instead, use the component directly:

```yaml
external_components:
  - source: github://Ahmed-max/esphome-levoit-humidifier@main
    components: [levoit_humidifier]

uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

levoit_humidifier:
  # update_interval: 5s   # how often the status is polled

switch:
  - platform: levoit_humidifier
    type: power
    name: Power
```

## Entities

Every entity takes the usual ESPHome options (`name`, `icon`, `internal`, ...) plus a `type`.

| Platform | `type` | Notes |
|---|---|---|
| `switch` | `power` | |
| `switch` | `display` | display on/off |
| `switch` | `auto_stop` | stop misting when the target is reached ("Auto Off") |
| `select` | `mode` | `Manual` / `Auto` |
| `number` | `target_humidity` | 30–80 %, switches to Auto |
| `number` | `mist_level` | 1–2, switches to Manual |
| `sensor` | `humidity` | % |
| `sensor` | `temperature` | °C |
| `binary_sensor` | `low_water` | tank empty |
| `binary_sensor` | `water_tank_lifted` | tank removed |
| `binary_sensor` | `mist_active` | mist currently running |
| `binary_sensor` | `auto_stopped` | target reached, paused by auto stop |
| `text_sensor` | `mcu_version` | firmware of the humidifier controller |

## Flashing

> Back up the stock firmware first. Levoit does not publish it, so the backup is the
> only way back to the VeSync app.

1. Open the base and find the 6-pin `ISP` header next to the ESP32-SOLO-1C:
   `EN, GND, VDD, TXD, RXD, IO0`.
2. Connect a 3.3 V USB-UART adapter: adapter RX → `TXD`, adapter TX → `RXD`, GND → `GND`.
   Leave `VDD` unconnected and power the humidifier from its own adapter; a USB-UART
   3.3 V pin cannot supply the board.
3. Bridge `IO0` to `GND` and power on to enter the bootloader.
4. Back up the full 4 MB flash:
   ```bash
   esptool --port COMx --baud 460800 read-flash 0 ALL dual200s_stock.bin
   ```
5. In ESPHome, *Install → Manual download → Factory format*, then flash it:
   ```bash
   esptool --port COMx --baud 460800 write-flash 0 levoit-dual200s.factory.bin
   ```
6. Remove the `IO0` bridge and power-cycle. Later updates go over Wi-Fi.

To go back to stock, write the backup to address 0.

## Credits

Structure inspired by [tuct/levoit](https://github.com/tuct/levoit) (Levoit air purifiers).
