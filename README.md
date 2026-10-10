# ESPHome Levoit Humidifier

[![Compile](https://github.com/Ahmed-max/esphome-levoit-humidifier/actions/workflows/compile.yml/badge.svg)](https://github.com/Ahmed-max/esphome-levoit-humidifier/actions/workflows/compile.yml)

> [!WARNING]
> **Early development.** This works on the author's Dual 200S, but it is young, has only been
> tested on one unit (MCU firmware 2.0.02) and may change without notice. Back up the stock
> firmware before flashing and expect rough edges. Bug reports with logs are very welcome.

Local, cloud-free control of Levoit smart humidifiers with ESPHome. The ESP32
module that ships inside the humidifier is reflashed with ESPHome and talks to the
humidifier's own controller over UART, so no extra hardware is needed.

| Model | Code | Status |
|---|---|---|
| Dual 200S | LUH-D301S | 🧪 early development, tested on one unit (MCU firmware 2.0.02) |

The UART protocol is documented in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Usage

The quickest route is the ready-made package, which brings in the board settings,
UART and every entity:

```yaml
esphome:
  name: levoit-dual200s
  friendly_name: Levoit Dual 200S

packages:
  levoit:
    url: https://github.com/Ahmed-max/esphome-levoit-humidifier
    ref: main
    files: [packages/dual200s.yaml]
    refresh: 0s   # always build the latest version

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
| `switch` | `auto_stop` | stop misting when the target is reached ("Auto Off"); kept across reboots |
| `select` | `mode` | `Manual` / `Auto`; switches the humidifier on |
| `number` | `target_humidity` | 30–80 %, only in Auto mode while on |
| `number` | `mist_level` | 1–2; switches on and to Manual |
| `number` | `timer` | minutes until the power flips (off if running, on if off), shows the remaining time; 0 cancels. Lights the timer icon |
| `sensor` | `humidity` | % |
| `sensor` | `temperature` | °C |
| `binary_sensor` | `low_water` | tank empty |
| `binary_sensor` | `water_tank_lifted` | tank removed |
| `binary_sensor` | `mist_active` | mist currently running |
| `binary_sensor` | `auto_stopped` | target reached, paused by auto stop |
| `binary_sensor` | `humidity_high` | humidity above 80 % |
| `binary_sensor` | `clean_reminder` | tank-cleaning reminder from the controller |
| `button` | `reset_clean_reminder` | clear the tank-cleaning reminder |
| `event` | `button` | long-presses on the device: `pairing`, `factory_reset`, `test_mode` |
| `text_sensor` | `mcu_version` | firmware of the humidifier controller |

## Same rules as the VeSync firmware

Changes the stock firmware refuses are refused here too and the entity snaps back:
mode, mist level and target need water and the tank in place, the target can only be set
in Auto mode while the humidifier is on, and the display only while it is on. Choosing a
mode or mist level switches the humidifier on, like the app does.

## Display behaviour

The display icons follow the stock firmware: the Wi-Fi icon is solid while connected,
blinks while the fallback hotspot is up and is off otherwise; the timer icon is lit while a
timer runs; the display cannot be changed while the humidifier is off.

The long-presses that started Wi-Fi pairing or a factory reset in the VeSync firmware are
reported as `event` entities instead, so nothing on the ESP is wiped by a stray press.

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
