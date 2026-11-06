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

## 3D Printable STL Files (Parametric Assembly)
All components are unified into a single parametric model in **SolidCraft** ([`bullseye.sc`](file:///D:/gemini/Projects/bullseye/bullseye.sc)). You can print the full plate or individual parts:

| File | Part | Description | Print Time / Settings |
| :--- | :--- | :--- | :--- |
| **[ullseye-full-plate.stl](file:///D:/gemini/Projects/bullseye/bullseye-full-plate.stl)** | **Complete Print Plate** | All 3 parts arranged on a single build plate (Drill Mount with 6x magnets, Front Case with 3-lug bayonet track, Twist-Lock Rear Lid with 6x magnets). | 0.20mm layer height (0.4mm nozzle), 3–4 walls, 20% infill. |
| **[ullseye-puck-front.stl](file:///D:/gemini/Projects/bullseye/bullseye-puck-front.stl)** | Front Bezel & Body | 54 mm body, 20 mm depth, 33.6 mm window (matches 33.4mm VA), 39.0 mm glass bore, 26.6 mm chin slot, 48.6 mm battery bay, 3-entry twist-lock bayonet track. | Print face-down on build plate. |
| **[ullseye-puck-back.stl](file:///D:/gemini/Projects/bullseye/bullseye-puck-back.stl)** | Twist-Lock Rear Lid | 48.4 mm bayonet lid with 3x locking lugs (60-deg twist-lock with solid stop), coin/thumb twist slot, 6x 10.2mm magnet pockets. | Print flat on build plate. |
| **[ullseye-mount.stl](file:///D:/gemini/Projects/bullseye/bullseye-mount.stl)** | Tool Cradle Mount | 54.6 mm cradle collar (smooth 0.3mm radial docking for 54mm puck), 6x 10.2mm magnet pockets at 60-deg spacing, 4.5 mm wide strap slots. | Print flat on build plate. |

---

## Hardware Bill of Materials (BOM)
| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **Main Puck Board** | Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2, 1.28" GC9A01 IPS LCD, CST816S touch, QMI8658 6-axis IMU) | ~$20 – $29 |
| **Battery** | 3.7V 400mAh – 500mAh LiPo pouch battery (size 502535 with 1.25mm 2-pin JST) | ~$3 – $5 |
| **Power Switch** | Micro slide switch (SS-12D00 / SS-12F15) inline with battery positive lead | ~$0.50 |
| **Magnets** | 12x 10x2mm Neodymium disc magnets (6 for puck lid + 6 for drill mount plate) | ~ |
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
