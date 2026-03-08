# DustCapIno INDI Driver

INDI driver for the **DustCapIno observatory controller**.

The driver communicates with the DustCapIno firmware over a serial connection
and provides control of a motorized telescope dust cap, flat-field illumination
panel and environmental sensors.

## Features

- Motorized telescope dust cap
- Flat-field illumination panel with dimming
- DHT22 temperature and humidity sensor
- Device diagnostics and watchdog
- Automatic serial port detection
- Safety system for flat-field light

## Firmware

Compatible with **DustCapIno firmware v1.4+**

Firmware is available in the `firmware/` directory.

## Build
