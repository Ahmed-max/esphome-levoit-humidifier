# Changelog

Versions follow [semantic versioning](https://semver.org). While the version starts with `0.`,
any release may change entities or options; check this file before updating.

## v0.1.1

### Fixed
- The MQTT humidifier entity's modes show as Auto / Low / High instead of lowercase
  (Home Assistant shows custom modes exactly as sent).

## v0.1.0 — first release

Early development: tested on one Dual 200S (MCU firmware 2.0.02).

### Added
- `levoit_humidifier` component for the Levoit Dual 200S (LUH-D301S), talking to the
  humidifier's controller over UART through its built-in ESP32-SOLO-1C.
- Controls: power, mode (Auto / Low / High, like the button on the device), target humidity,
  timer (lights the timer icon), display, Auto Off.
- Sensors: humidity, low water, water tank lifted, mist active, target humidity reached,
  clean tank reminder, humidity high, error, MCU firmware. Optional: mist level, temperature.
- Reset clean tank reminder button.
- Optional Home Assistant humidifier entity over MQTT (`mqtt_humidifier: true`), placed on
  the same device by MAC address. Disabling it removes the entity again.
- `wifi_icon` option: light the Wi-Fi icon on Wi-Fi, or only while Home Assistant is
  connected.
- Display behaviour of the stock firmware: Wi-Fi icon, timer icon, display refused while off.
- Ready-made package `packages/dual200s.yaml`, pinnable to a release through `levoit_ref`.
- Protocol documentation (`docs/PROTOCOL.md`) and host unit tests with captured frames.

### Behaviour notes
- Mode changes are always sent, also without water; the humidifier decides whether it can
  mist. Water low is only reported while running (the MCU reports it all the time while off).
- A timer ends early when the power is switched its way by other means, so it never turns
  the humidifier back on after a manual off.
- Auto Off is saved per hub `id`. Builds from `main` before this release saved it under a
  shared key, so it resets once.
