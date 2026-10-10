# Levoit Dual 200S (LUH-D301S-WEU) — ESP32 ⇄ MCU UART protocol

Recovered by static analysis of the stock VeSync ESP32 firmware 1.0.09 (built 2023-06-26)
with Ghidra 12.1.4, then confirmed on a live unit (MCU firmware 2.0.02).

Confidence tags: **[code]** read directly from decompiled code / constants,
**[infer]** reasoned from code, needs confirmation with a capture.

## Physical link

| Item | Value | Source |
|---|---|---|
| ESP32 UART | UART2 | [infer] `vesync_device_info_get_int` flag 0x11/0x12 → 2 |
| ESP32 TX → MCU | GPIO17 | confirmed on hardware |
| ESP32 RX ← MCU | GPIO16 | confirmed on hardware |
| Baud | 9600 8N1 | confirmed on hardware |
| Console / ISP header | UART0 (GPIO1/3) 115200 | [code] boot log |

Same pins/baud as the Levoit LV600S humidifier ESPHome port.

## Transport frame  [code]

```
+------+------+-----+--------+--------+----------+------------------+
| 0xA5 | ctrl | seq | len_lo | len_hi | checksum | payload[len]     |
+------+------+-----+--------+--------+----------+------------------+
checksum = 0xFF - (sum of every other byte in the frame, incl. 0xA5) & 0xFF
           (i.e. all bytes including checksum sum to 0xFF mod 256)
len      < 0x5DD
```

`ctrl` bits:

| bits | meaning |
|---|---|
| 0x0F | protocol version, must be `2` |
| 0x10 | frame is an ACK |
| 0x20 | sender wants an ACK |
| 0x40 | error flag (in ACKs) |

- ESP → MCU command: `ctrl = 0x22`, `seq` increments per new frame. Only one
  outstanding frame; resent by a 1000 ms timer until the ACK with the same `seq` arrives.
- ACK: `ctrl = 0x12` (or `0x52` on error), `seq` copied from the frame being acked.
- MCU → ESP frames: low nibble 2, bit 0x10 clear. ESP stores their `seq` for its ACK.

## Payload  [code]

```
[0] 0x01          payload version (receiver rejects >= 2)
[1] cmd_lo
[2] cmd_hi        16-bit opcode, little endian
[3] status/err    0 in commands; error code in ACKs (0x34 = payload < 4, 0x1F = bad version)
[4..] data
```

## ESP → MCU commands  [code]

| Opcode | Function | Data |
|---|---|---|
| 0xA000 | set_power | `[on]` 0/1 |
| 0xA105 | set_display | `[brightness]` 0 = off, 100 = on |
| 0xA129 | set_wifi_led | `[mode 0/1/2, on_ms u16, off_ms u16, x]` [infer names] |
| 0xA260 | set_manual_mode_lvl | `[0, 1, level]`, level 1–2 |
| 0xA26A | set_timer_logo | `[on]` timer icon |
| 0xA5E5 | set_auto_stop | `[on]` stop when target reached |
| 0x4080 | set_auto_mode | `[target, target-5, target+5, 2, 1, 1]` |
| 0x40C0 | query_mcu_status | none → ACK carries status block |
| 0x40C3 | set_container_clean | `[on]` |
| 0xD004 | production_progress | 3 bytes (factory) |
| 0xD005 | production_result | 2 bytes (factory) |
| 0xD007 | uart_test | (factory) |
| 0xD101 | reboot_mcu | `[param]` |

## MCU → ESP frames  [code]

| Opcode | Meaning | ESP must ACK? |
|---|---|---|
| 0x40C1 | status report (block below) | no |
| 0x40C4 | MCU reset container-clean state | no |
| 0xD102–0xD103 | button event, `data[0]`: 2, 3, 4 (4 = "Device" → likely pairing/reset) | yes if ctrl&0x20 |
| 0xD104 | network-config / reset request | yes if ctrl&0x20 |

ACK reply format: `A5 12 <their seq> 04 00 <cs> 01 <cmd_lo> <cmd_hi> 00`.

## Status block (data of 0x40C1, and of the ACK to 0x40C0)  [code]

| Offset | Field | Notes |
|---|---|---|
| 0 | MCU fw patch | version = `[2].[1].[0]` |
| 1 | MCU fw minor | |
| 2 | MCU fw major | |
| 3 | enabled | power on |
| 4 | container | water tank present |
| 5 | water_lacks | tank empty |
| 6 | auto_stop_status | auto-stop reached |
| 7 | display_config | display on |
| 8 | fog_status | mist currently on |
| 9 | tgt_humidity | % |
| 10 | cur_humidity | % |
| 11 | cur_temp | unit unknown [infer °C] |
| 12 | mode | 0 = manual, 1 = auto |
| 13 | mist_lvl | 1–2 |
| 14 | other_exception | error flags |
| 20 | container_clean_status | |

Total copied: 25 bytes.

## Example frames (computed)

```
power on      A5 22 01 05 00 90 01 00 A0 00 01
power off     A5 22 02 05 00 90 01 00 A0 00 00
query status  A5 22 03 04 00 30 01 C0 40 00
mist level 2  A5 22 04 07 00 27 01 60 A2 00 00 01 02
auto, 50 %    A5 22 05 0A 00 CE 01 80 40 00 32 2D 37 02 01 01
display off   A5 22 06 05 00 86 01 05 A1 00 00
```

## Still to confirm with a capture

- Meaning of `set_wifi_led` fields, button codes 2/3/4, `cur_temp` unit, `other_exception` bits.
- Whether the MCU needs a periodic query / heartbeat, or pushes 0x40C1 on its own.
