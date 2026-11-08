<p align="center">
  <img src="assets/bullseye-logo.jpg" alt="Bullseye Logo" width="220" />
</p>

# Bullseye 🎯

**Open-source Smart Digital Spirit Level & Precision Angle Guide for Power Tools**

**Bullseye** is a DIY digital alignment puck that mounts onto cordless drills, drill presses, miter saws, and handheld power tools. It provides real-time, on-screen visual feedback for perpendicularity (90° square drilling), surface leveling, and custom bevel angle guidance.

---

## Features
* **Gravity Bullseye Mode:** 2D circular spirit level ensuring holes are drilled perfectly perpendicular to the ground or wall.
* **Auto Wall / Bench Detection:** Automatically switches between vertical wall drilling (plumb) and horizontal bench leveling with hysteresis.
* **6-Axis Sensor Fusion & Silicon LPF:** Fuses 3-axis gyro angular velocity with accelerometer gravity, backed by hardware digital low-pass filtering on the QMI8658 silicon to completely reject motor vibration chatter.
* **Target Angle Presets:** Fast one-touch presets for standard bevel angles (0°, 15°, 30°, 45°) with green-lock visual alignment within ±0.5°.
* **One-Touch Tare / Zero:** Calibrate against any angled workpiece or jig with a single tap on the screen.
* **Fluid Viscosity Damping:** Mineral spirit physics simulation running at 50 FPS on a double-buffered PSRAM DMA sprite for smooth, organic bubble movement.
* **Smart Power Management:** Auto-dim after 60s, deep sleep after 3 mins of inactivity, and instant wake-on-touch or motion.
* **Wireless OTA & Setup AP:** Wireless firmware flashing via ArduinoOTA and captive portal Wi-Fi configuration (`DrillPuck-Setup`).

---

## 3D CAD & Enclosure Design
The primary canonical enclosure is modeled in **Autodesk Fusion 360** and distributed as a universal STEP assembly:

* **Primary Model:** [`BullsEye.step`](BullsEye.step)

### Components in Assembly
| Component | Description | Recommended Print Settings |
| :--- | :--- | :--- |
| **Puck Top** | Front bezel & main enclosure housing the 1.28" round Waveshare board, display viewport, chin recess, and LiPo chamber. | Print face-down, 0.20mm layer height (0.4mm nozzle), 3–4 walls, 20% infill. |
| **Puck Lid** | Threaded rear closure lid featuring 6x 10x2mm neodymium magnet pockets and coin twist slot. | Print flat, 0.20mm layer height, 100% infill for magnet solid beds. |
| **Mount** | Power tool backplate cradle with 6x 10x2mm magnet pockets and dual strap/zip-tie mounting slots. | Print flat, PETG or Tough PLA, 4 walls, 30% infill. |

> Modern slicers (Bambu Studio, OrcaSlicer, PrusaSlicer 2.5+, and Cura) support direct `.step` import, ensuring smooth cylindrical surfaces and precise thread engagement without STL polygonal mesh faceting.

---

## Hardware Bill of Materials (BOM)
| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **Main Puck Board** | Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2, 1.28" GC9A01 IPS LCD, CST816S touch, QMI8658 6-axis IMU) | ~$20 – $29 |
| **Battery** | 3.7V 400mAh – 500mAh LiPo pouch battery (size 502535 with 1.25mm 2-pin JST) | ~$3 – $5 |
| **Power Switch** | Micro slide switch (SS-12D00 / SS-12F15) inline with battery positive lead | ~$0.50 |
| **Magnets** | 12x 10x2mm Neodymium disc magnets (6 for puck lid + 6 for drill mount plate) | ~$3 |
| **Filament** | PETG or Tough PLA | ~$1 |

---

## Software & Firmware Build
1. Install [PlatformIO](https://platformio.org/) in VS Code or CLI.
2. Build and upload via USB:
   ```bash
   pio run -e waveshare_esp32s3_touch_lcd_128 -t upload
   ```
3. Or update wirelessly via Wi-Fi OTA:
   ```bash
   pio run -e ota -t upload
   ```
