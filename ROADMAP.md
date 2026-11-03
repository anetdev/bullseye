# Bullseye Roadmap & Action Items

## Phase 1: Hardware Bring-Up & Validation
- [x] Connect Waveshare ESP32-S3-Touch-LCD-1.28 over USB-C.
- [x] Flash baseline diagnostic sketch (`src/main.cpp`) to verify:
  - LCD backlight on GPIO 2
  - CST816S touch controller at `0x15`
  - QMI8658 IMU at `0x6B`
  - Battery voltage sensing on GPIO 1
- [x] Verify GC9A01 LCD graphics initialization and touch coordinates.
- [x] Enable and verify wireless ArduinoOTA flashing over Wi-Fi.

## Phase 2: Sensor Fusion & Math Engine
- [x] Implement 3D unit gravity vector normalization (mg to unit length).
- [x] Build one-touch Tare / Zero routine.
- [x] Dual-mode auto-switch: Wall Drilling (upright 90°) vs Bench Leveling (horizontal).
- [x] Add WiFiManager captive portal AP setup (`Bullseye-Setup`) to eliminate hardcoded Wi-Fi.
- [x] Enable QMI8658 hardware silicon digital low-pass filter (0x55 on CTRL5).
- [x] Implement 6-Axis Complementary Sensor Fusion (fusing Gyro angular rates with Accel gravity).

## Phase 3: Display & Power Management
- [x] High-performance 50 FPS double-buffered DMA sprite canvas via LovyanGFX.
- [x] Digital Bullseye Bubble Level with mineral spirit fluid viscosity damping.
- [x] Target Angle Presets (0°, 15°, 30°, 45°) with bevel icon button.
- [x] Touch interactions (center tap to tare, top-right tap to cycle preset).
- [x] Hardware power button handling (GPIO 0: short press cycles presets, long press enters sleep).
- [x] Automatic battery power management (auto-dim at 60s, deep sleep at 3 mins, wake on touch/button).

## Phase 4: Enclosure & Tool Mounting
- [x] Design 3D printable circular puck enclosure in SolidCraft CAD (`bullseye.sc`).
- [x] Add physical power switch cutout (9×5mm slot) on puck wall for inline LiPo switch.
- [x] Include battery bay for 400mAh LiPo pouch.
- [x] Design magnetic baseplate and clip mount for Craftsman V20 drill backplate.
- [x] Export updated printable STLs (`bullseye-full-plate.stl`, `bullseye-puck-front.stl`, `bullseye-puck-back.stl`, `bullseye-mount.stl`).
- [ ] Print test puck in PETG / PLA.

## Phase 5: Field Testing & Enhancements
- [ ] Live drilling test on drill press and handheld cordless drill.
- [ ] Fine-tune vibration damping filter while motor is running under load.
- [ ] (Optional) BLE telemetry / Home Assistant or web app drill-logging.
