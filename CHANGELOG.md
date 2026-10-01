# Changelog

## Unreleased
- Require and detect INDI 2.2.4 or newer during CMake configuration
- Add a KStars Flatpak build mode that links to its exact bundled INDI runtime
- Install INDI driver registration metadata
- Pin current Arduino AVR, Servo and DHT build dependencies in `sketch.yaml`
- Fix the brightness command delimiter to match the firmware protocol
- Add a serial-number-matched udev installer and documentation for the
  persistent `/dev/dustcapino` device name

## v1.99
- Improved serial auto-detection
- Improved servo diagnostics
- Smart polling system
- Reconnect watchdog
