# Bullseye: Smart Digital Spirit Level & Precision Angle Guide

## Project Purpose & Goals
Open-source DIY smart tool interface and digital angle guide. The device magnetically or mechanically attaches to cordless drills, drill presses, miter saws, and power tools to provide real-time digital bullseye leveling, drill angle tracking, and surface alignment.

## Core Hardware Specifications
* **Target Board:** Waveshare ESP32-S3-Touch-LCD-1.28
* **MCU:** ESP32-S3R2 (Xtensa dual-core LX7 @ 240MHz, 16MB Flash, 2MB PSRAM, Wi-Fi 4, BLE 5.0)
* **Display:** 1.28-inch round IPS LCD (240x240, 65K colors, GC9A01A via SPI)
* **Touch Controller:** CST816S capacitive touch (I2C)
* **IMU (Sensor):** QMI8658 6-axis accelerometer & gyroscope (I2C) with hardware silicon low-pass filter
* **Battery:** 3.7V 400-500mAh LiPo via onboard MX1.25 connector with ETA6003 charge management (40×21×8mm battery bay)
* **ADC Battery Sense:** GPIO 1 (via 2:1 voltage divider)

## Hardwired GPIO Pinout
* **Display (GC9A01):** MOSI=GPIO11, SCLK=GPIO10, CS=GPIO9, DC=GPIO8, RST=GPIO14, Backlight=GPIO2
* **I2C Bus (Shared Touch + IMU):** SDA=GPIO6, SCL=GPIO7
  * CST816S Touch: INT=GPIO5, RST=GPIO13, I2C Address `0x15`
  * QMI8658 IMU: INT1=GPIO46, INT2=GPIO45, I2C Address `0x6B`
* **Battery Monitor:** GPIO1
* **Button:** GPIO0 (BOOT button)

## Key Architectural Decisions
1. **Graphics Engine:** LovyanGFX with a 50 FPS double-buffered DMA sprite canvas in PSRAM for mineral spirit fluid viscosity simulation.
2. **Sensor Fusion:** 6-axis Complementary Filter fusing 3-axis gyro angular velocity with accelerometer gravity, backed by QMI8658 hardware silicon LPF.
3. **Tare / Zeroing:** Touch gesture or tap on screen to set reference plane (e.g. workpiece angled surface).
4. **Tooling / IDE:** PlatformIO with dual USB and Wi-Fi OTA flash environments.
5. **CAD / Enclosure:** Primary mechanical enclosure designed in **Autodesk Fusion 360** and tracked canonically as [`BullsEye.step`](BullsEye.step) (`Puck Top`, `Puck Lid`, `Mount`). No raw STL files tracked; modern slicers import STEP directly for full boundary surface fidelity.

## Current State
* Hardware tested, fully calibrated, and operational.
* Wireless OTA flashing functional over local Wi-Fi.
* Canonical mechanical assembly in `BullsEye.step`.
