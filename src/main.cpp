#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <QMI8658.h>
#include "pin_config.h"
#include "logo_data.h"
#include "LGFX_ESP32S3_GC9A01.h"

// Bullseye — Smart Digital Bullseye Level & Precision Angle Guide
// Real-Time 3D Bullseye Spirit Level with 6-Axis Gyro Fusion & Silicon LPF

void rebootToBootloader() {
    Serial.println("[System] Rebooting into ROM Download Bootloader...");
    delay(100);
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

// --- Display & Layout Constants ---
constexpr int CENTER_X          = 120;
constexpr int CENTER_Y          = 120;
constexpr float BUBBLE_MAX_DIST = 85.0f;
constexpr int HEADER_Y          = 20;

constexpr int BTN_CAL_X         = 28;
constexpr int BTN_CAL_Y         = 26;
constexpr int BTN_CAL_W         = 54;
constexpr int BTN_CAL_H         = 24;
constexpr int BTN_CAL_RADIUS    = 12;

constexpr int BTN_PRESET_X      = 152;
constexpr int BTN_PRESET_Y      = 26;
constexpr int BTN_PRESET_W      = 60;
constexpr int BTN_PRESET_H      = 24;
constexpr int BTN_PRESET_RADIUS = 12;

LGFX_Waveshare128 lcd;
LGFX_Sprite canvas(&lcd);
QMI8658 qmi;

// Sensor & System Status
bool imuOk = false;
bool isVerticalWallMode = false;
bool isVibrating = false;

// Power Management State
unsigned long lastActivityTime = 0;
bool isDisplayDimmed = false;
bool isStandby = false;

// Filtered acceleration components (in g's)
float fx = 0.0f;
float fy = 0.0f;
float fz = 0.0f;

// 6-Axis Gyro Calibration & Fused Angles
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;
float fusedRoll = 0.0f;
float fusedPitch = 0.0f;
bool fusionInitialized = false;

// Persistent Desk Calibration (saved to Flash NVS)
Preferences prefs;
bool hasDeskCal = false;
float deskCalX = 0.0f;
float deskCalY = 0.0f;

// Calibration State Machine
enum CalibState {
    CALIB_IDLE = 0,
    CALIB_COUNTDOWN,
    CALIB_SAMPLING,
    CALIB_SAVED,
    CALIB_RESET
};
CalibState calibState = CALIB_IDLE;
unsigned long calibStartTime = 0;
float calibSumX = 0.0f;
float calibSumY = 0.0f;
int calibSampleCount = 0;

// Tare offsets (in g's)
float tareX = 0.0f;
float tareY = 0.0f;
float tareZ = 0.0f;
bool isTared = false;

// Quick Target Angle Presets (in degrees)
const float TARGET_PRESETS[] = { 0.0f, 15.0f, 30.0f, 45.0f };
const int NUM_PRESETS = sizeof(TARGET_PRESETS) / sizeof(TARGET_PRESETS[0]);
int currentPresetIdx = 0; // 0 = 0.0° (Level / Plumb)

float readBatteryVoltage() {
    // Waveshare ESP32-S3-Touch-LCD-1.28 has a 3:1 resistor divider (200k top / 100k bottom)
    // Multi-sample ADC for clean, stable battery voltage reading
    uint32_t sumMv = 0;
    constexpr int NUM_SAMPLES = 16;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sumMv += analogReadMilliVolts(PIN_BATTERY_ADC);
        delayMicroseconds(50);
    }
    float avgMv = (float)sumMv / NUM_SAMPLES;
    return (avgMv * 3.0f) / 1000.0f;
}

int getBatteryPercent(float v) {
    if (v >= 4.18f) return 100;
    if (v <= 3.30f) return 0;
    // Piecewise LiPo discharge curve
    if (v >= 4.00f) return 85 + (int)((v - 4.00f) / 0.18f * 15.0f);
    if (v >= 3.85f) return 60 + (int)((v - 3.85f) / 0.15f * 25.0f);
    if (v >= 3.75f) return 40 + (int)((v - 3.75f) / 0.10f * 20.0f);
    if (v >= 3.65f) return 20 + (int)((v - 3.65f) / 0.10f * 20.0f);
    if (v >= 3.50f) return 10 + (int)((v - 3.50f) / 0.15f * 10.0f);
    return (int)((v - 3.30f) / 0.20f * 10.0f);
}

void enterStandby() {
    isStandby = true;
    canvas.fillScreen(TFT_BLACK);
    canvas.pushSprite(0, 0);
    lcd.setBrightness(0); // Cut PWM backlight to 0 to prevent battery drain!
    lcd.sleep();
    setCpuFrequencyMhz(80); // Drop CPU clock to save battery power
    Serial.println("[Power] Standby mode activated: Backlight OFF, Display Sleep, Touch & Motion active.");
}

void wakeFromStandby() {
    setCpuFrequencyMhz(240); // Restore full performance
    lcd.wakeup();
    lcd.setBrightness(220);
    isStandby = false;
    isDisplayDimmed = false;
    lastActivityTime = millis();
    Serial.println("[Power] Woke from Standby mode!");
}

void drawCalibrationOverlay(unsigned long now) {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextDatum(textdatum_t::middle_center);

    if (calibState == CALIB_COUNTDOWN) {
        int elapsed = (int)(now - calibStartTime);
        int remainingSec = 3 - (elapsed / 1000);
        if (remainingSec < 1) remainingSec = 1;

        // Visual guide rings
        canvas.drawCircle(CENTER_X, CENTER_Y, 75, canvas.color565(40, 60, 90));
        canvas.drawCircle(CENTER_X, CENTER_Y, 76, canvas.color565(40, 60, 90));
        canvas.drawCircle(CENTER_X, CENTER_Y, 45, canvas.color565(60, 90, 130));

        // Big countdown number in center
        char numBuf[8];
        snprintf(numBuf, sizeof(numBuf), "%d", remainingSec);
        canvas.setTextColor(TFT_YELLOW);
        canvas.drawString(numBuf, CENTER_X, CENTER_Y - 2, &fonts::Font4);

        // Header and instructional text
        canvas.setTextColor(canvas.color565(120, 220, 160));
        canvas.drawString("DESK CALIBRATION", CENTER_X, 32, &fonts::Font2);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("PLACE FLAT ON DESK", CENTER_X, 168, &fonts::Font0);
        canvas.setTextColor(canvas.color565(255, 120, 120));
        canvas.drawString("HANDS OFF IN 3s!", CENTER_X, 185, &fonts::Font0);
        canvas.setTextColor(canvas.color565(140, 140, 160));
        canvas.drawString("(Tap anytime to cancel)", CENTER_X, 203, &fonts::Font0);
    } else if (calibState == CALIB_SAMPLING) {
        // Pulsing rings during measurement
        uint16_t pulseCol = ((now / 200) % 2 == 0) ? TFT_CYAN : canvas.color565(0, 140, 210);
        canvas.drawCircle(CENTER_X, CENTER_Y, 75, pulseCol);
        canvas.drawCircle(CENTER_X, CENTER_Y, 76, pulseCol);
        canvas.drawCircle(CENTER_X, CENTER_Y, 45, pulseCol);

        canvas.setTextColor(TFT_CYAN);
        canvas.drawString("SAMPLING...", CENTER_X, CENTER_Y - 4, &fonts::Font4);

        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("DO NOT TOUCH", CENTER_X, 172, &fonts::Font2);
        canvas.setTextColor(canvas.color565(180, 180, 180));
        canvas.drawString("Measuring gravity vector...", CENTER_X, 195, &fonts::Font0);
    } else if (calibState == CALIB_SAVED) {
        canvas.fillCircle(CENTER_X, CENTER_Y, 55, canvas.color565(0, 70, 30));
        canvas.drawCircle(CENTER_X, CENTER_Y, 55, TFT_GREEN);
        canvas.drawCircle(CENTER_X, CENTER_Y, 56, TFT_GREEN);

        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("SAVED!", CENTER_X, CENTER_Y - 12, &fonts::Font4);

        char offStr[32];
        float offPitch = asin(constrain(deskCalX, -1.0f, 1.0f)) * (180.0f / M_PI);
        float offRoll = asin(constrain(-deskCalY, -1.0f, 1.0f)) * (180.0f / M_PI);
        snprintf(offStr, sizeof(offStr), "X:%+.1f\xF8 Y:%+.1f\xF8", offPitch, offRoll);
        canvas.setTextColor(canvas.color565(140, 255, 170));
        canvas.drawString(offStr, CENTER_X, CENTER_Y + 16, &fonts::Font0);

        canvas.setTextColor(TFT_GREEN);
        canvas.drawString("FLASH NVS UPDATED", CENTER_X, 180, &fonts::Font2);
        canvas.setTextColor(canvas.color565(180, 220, 180));
        canvas.drawString("Level is now 0.0\xF8", CENTER_X, 200, &fonts::Font0);
    } else if (calibState == CALIB_RESET) {
        canvas.fillCircle(CENTER_X, CENTER_Y, 55, canvas.color565(80, 40, 0));
        canvas.drawCircle(CENTER_X, CENTER_Y, 55, TFT_ORANGE);
        canvas.drawCircle(CENTER_X, CENTER_Y, 56, TFT_ORANGE);

        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("RESET!", CENTER_X, CENTER_Y - 12, &fonts::Font4);

        canvas.setTextColor(TFT_ORANGE);
        canvas.drawString("FACTORY ZERO", CENTER_X, CENTER_Y + 14, &fonts::Font0);

        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("Calibration Cleared", CENTER_X, 182, &fonts::Font2);
    }

    canvas.pushSprite(0, 0);
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(PIN_BATTERY_ADC, INPUT);

    // Disable all RF radios (Wi-Fi & Bluetooth) to maximize LiPo battery life (~10-12 hours)
    esp_wifi_stop();
    esp_wifi_deinit();

    Serial.println("\n========================================");
    Serial.println("         Bullseye Smart Level           ");
    Serial.println("            Drill Straight!             ");
    Serial.println("              by aNetDev                ");
    Serial.println("========================================");

    // 1. Initialize LCD Display
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(220);
    lcd.fillScreen(TFT_BLACK);

    // 2. Double-Buffered Canvas (240x240) in PSRAM
    canvas.setColorDepth(16);
    canvas.createSprite(240, 240);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setAttribute(attribute_t::utf8_switch, false); // Enable extended CP437 glyphs (\xF8 = °)

    // Initial Splash with Official Bullseye Logo
    canvas.fillScreen(TFT_BLACK);
    canvas.drawJpg(BULLSEYE_LOGO_JPG, BULLSEYE_LOGO_LEN, (240 - 180) / 2, 8);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("Drill Straight!", CENTER_X, 194, &fonts::Font2);
    canvas.setTextColor(canvas.color565(140, 190, 230));
    canvas.drawString("by aNetDev", CENTER_X, 210, &fonts::Font0);
    canvas.setTextColor(TFT_CYAN);
    canvas.drawString("Calibrating IMU...", CENTER_X, 224, &fonts::Font0);
    canvas.pushSprite(0, 0);

    // 3. Initialize Shared I2C Bus & QMI8658 6-Axis IMU
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    imuOk = qmi.begin(Wire, QMI8658_ADDRESS_HIGH);
    if (!imuOk) imuOk = qmi.begin(Wire, QMI8658_ADDRESS_LOW);

    if (imuOk) {
        qmi.setAccelRange(QMI8658_ACCEL_RANGE_4G);
        qmi.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
        qmi.setGyroRange(QMI8658_GYRO_RANGE_512DPS);
        qmi.setGyroODR(QMI8658_GYRO_ODR_125HZ);
        qmi.enableSensors(QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);

        // Hardware Silicon Low-Pass Filter:
        // Write to QMI8658 CTRL5 (Register 0x06):
        // Bit 0: aLPF_EN (Accel LPF Enable), Bits 1-2: aLPF_BW (5.39% ODR)
        // Bit 4: gLPF_EN (Gyro LPF Enable), Bits 5-6: gLPF_BW (5.39% ODR)
        // 0x55 = 01010101b enables hardware digital filtering on both sensors directly on the silicon!
        uint8_t imuAddr = QMI8658_ADDRESS_HIGH;
        Wire.beginTransmission(imuAddr);
        Wire.write(0x06); // CTRL5
        Wire.write(0x55); // Enable aLPF + gLPF
        if (Wire.endTransmission() != 0) {
            Wire.beginTransmission(QMI8658_ADDRESS_LOW);
            Wire.write(0x06);
            Wire.write(0x55);
            Wire.endTransmission();
        }

        // Stationary gyro bias sampling (100 readings across 500ms)
        float sumGx = 0, sumGy = 0, sumGz = 0;
        constexpr int CALIB_SAMPLES = 100;
        for (int i = 0; i < CALIB_SAMPLES; i++) {
            float gx = 0, gy = 0, gz = 0;
            qmi.readGyroDPS(gx, gy, gz);
            sumGx += gx;
            sumGy += gy;
            sumGz += gz;
            delay(5);
        }
        gyroBiasX = sumGx / (float)CALIB_SAMPLES;
        gyroBiasY = sumGy / (float)CALIB_SAMPLES;
        gyroBiasZ = sumGz / (float)CALIB_SAMPLES;

        Serial.printf("[IMU] QMI8658 6-Axis online. Hardware LPF 0x55 active. Gyro bias: (%.2f, %.2f, %.2f) dps\n",
                      gyroBiasX, gyroBiasY, gyroBiasZ);
    } else {
        Serial.println("[IMU] ERROR: QMI8658 not detected on I2C bus!");
    }

    // 4. Load Persistent Desk Calibration from Flash NVS
    prefs.begin("bullseye", false);
    hasDeskCal = prefs.getBool("has_cal", false);
    if (hasDeskCal) {
        deskCalX = prefs.getFloat("cal_x", 0.0f);
        deskCalY = prefs.getFloat("cal_y", 0.0f);
        Serial.printf("[NVS] Loaded Desk Calibration: offset X=%.4f (pitch), Y=%.4f (roll)\n", deskCalX, deskCalY);
    } else {
        Serial.println("[NVS] No saved Desk Calibration found. Using factory zero.");
    }
    prefs.end();

    lastActivityTime = millis();
    delay(1600);
}

void loop() {
    static unsigned long lastUpdate = 0;
    static unsigned long lastSerial = 0;
    static unsigned long lastSensor = 0;
    static unsigned long lastBatRead = 0;
    static unsigned long touchStartTime = 0;
    static bool wasTouched = false;
    static float smooth_bx = (float)CENTER_X;
    static float smooth_by = (float)CENTER_Y;
    static float cachedVBat = 0.0f;
    static int cachedBatPct = 0;

    unsigned long now = millis();

    // Software Bootloader trigger over Serial (e.g. 'b' or 'F' command)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'b' || c == 'B' || c == 'F') {
            rebootToBootloader();
        }
    }

    // If in Standby: poll touch and motion at low rate (every 50ms) to save battery
    if (isStandby) {
        delay(50);

        int32_t tx = 0, ty = 0;
        if (lcd.getTouch(&tx, &ty)) {
            wakeFromStandby();
            return;
        }

        if (imuOk) {
            float rx = 0, ry = 0, rz = 0;
            qmi.readAccel(rx, ry, rz);
            float m = sqrt(rx * rx + ry * ry + rz * rz);
            if (m > 100.0f) {
                rx /= m; ry /= m; rz /= m;
                float d = abs(rx - fx) + abs(ry - fy) + abs(rz - fz);
                if (d > 0.25f) { // Real pickup or tilt motion
                    fx = rx; fy = ry; fz = rz;
                    wakeFromStandby();
                    return;
                }
            }
        }
        return; // Don't run display rendering while in standby!
    }

    // Battery ADC Read (throttled to 5 seconds)
    if (now - lastBatRead >= 5000 || lastBatRead == 0) {
        lastBatRead = now;
        cachedVBat = readBatteryVoltage();
        cachedBatPct = getBatteryPercent(cachedVBat);
    }

    // 2. 6-Axis Sensor Fusion Read at 100 Hz (dt = 0.010 s)
    float gyroDPS_X = 0.0f, gyroDPS_Y = 0.0f, gyroDPS_Z = 0.0f;
    if (imuOk && (now - lastSensor >= 10)) {
        float dt = (now - lastSensor) / 1000.0f;
        if (dt <= 0.0f || dt > 0.1f) dt = 0.010f;
        lastSensor = now;

        float rawX = 0, rawY = 0, rawZ = 0;
        qmi.readAccel(rawX, rawY, rawZ);
        qmi.readGyroDPS(gyroDPS_X, gyroDPS_Y, gyroDPS_Z);

        gyroDPS_X -= gyroBiasX;
        gyroDPS_Y -= gyroBiasY;
        gyroDPS_Z -= gyroBiasZ;

        // Acceleration magnitude & dynamic vibration metric
        float mag = sqrt(rawX * rawX + rawY * rawY + rawZ * rawZ);
        float vibDev = abs(mag - 1000.0f) / 1000.0f;
        static float vibMetric = 0.0f;
        vibMetric = 0.90f * vibMetric + 0.10f * vibDev;
        isVibrating = (vibMetric > 0.16f);

        if (mag > 100.0f) {
            rawX /= mag;
            rawY /= mag;
            rawZ /= mag;
        }

        // Filtering factor at 100 Hz (fast and crisp, smoothed by silicon LPF)
        const float alpha = isVibrating ? 0.92f : 0.86f;
        fx = alpha * fx + (1.0f - alpha) * rawX;
        fy = alpha * fy + (1.0f - alpha) * rawY;
        fz = alpha * fz + (1.0f - alpha) * rawZ;

        // Motion detection with low-pass filtered gravity baseline
        static float baseFx = 0.0f, baseFy = 0.0f, baseFz = 0.0f;
        static bool baseInit = false;
        if (!baseInit) {
            baseFx = fx; baseFy = fy; baseFz = fz;
            baseInit = true;
        } else {
            // Baseline smoothly tracks (slow ~3s time constant so table resting doesn't trigger false wake)
            baseFx += 0.005f * (fx - baseFx);
            baseFy += 0.005f * (fy - baseFy);
            baseFz += 0.005f * (fz - baseFz);
        }

        float motionDelta = abs(fx - baseFx) + abs(fy - baseFy) + abs(fz - baseFz);
        if (motionDelta > 0.12f) { // Real tool pickup or tilt
            lastActivityTime = now;
            if (isDisplayDimmed) {
                lcd.setBrightness(220);
                isDisplayDimmed = false;
            }
        }
    }

    // Power management inactivity timeouts:
    // After 60s without touch/motion: dim screen
    if (!isDisplayDimmed && (now - lastActivityTime > 60000)) {
        lcd.setBrightness(35);
        isDisplayDimmed = true;
    }
    // After 120s (2 mins) without touch/motion: enter standby sleep
    if (now - lastActivityTime > 120000) {
        enterStandby();
    }

    // 3. 3D Tilt Calculation & Smart Mode Auto-Switch with Hysteresis
    // On the Waveshare ESP32-S3-Touch-LCD-1.28 hardware, the QMI8658 chip has its X-axis
    // aligned with the upright screen vertical axis. When upright (Wall Drill mode), |fx| > 0.60.
    // When flat on a table (Bench mode), |fz| > 0.60.
    if (isVerticalWallMode) {
        if (abs(fz) > 0.60f && abs(fx) < 0.60f) {
            isVerticalWallMode = false;
            fusionInitialized = false;
            Serial.println("[Mode] Switched to BENCH / DESK LEVEL");
        }
    } else {
        if (abs(fx) > 0.60f && abs(fz) < 0.60f) {
            isVerticalWallMode = true;
            fusionInitialized = false;
            Serial.println("[Mode] Switched to WALL DRILL");
        }
    }

    float accelRoll_deg = 0.0f;
    float accelPitch_deg = 0.0f;
    float totalTilt = 0.0f;

    if (isTared) {
        // True 3D relative angle between tare gravity reference and current gravity vector
        float dot = constrain(fx * tareX + fy * tareY + fz * tareZ, -1.0f, 1.0f);
        totalTilt = acos(dot) * (180.0f / M_PI);

        // Cross-product (tare x current) gives exact relative rotation axis and sine-projections
        float cx = tareY * fz - tareZ * fy;
        float cy = tareZ * fx - tareX * fz;
        accelRoll_deg = asin(constrain(cx, -1.0f, 1.0f)) * (180.0f / M_PI);
        accelPitch_deg = asin(constrain(cy, -1.0f, 1.0f)) * (180.0f / M_PI);
    } else {
        if (isVerticalWallMode) {
            accelRoll_deg = asin(constrain(-fy, -1.0f, 1.0f)) * (180.0f / M_PI);
            accelPitch_deg = asin(constrain(-fz, -1.0f, 1.0f)) * (180.0f / M_PI);
            totalTilt = sqrt(accelRoll_deg * accelRoll_deg + accelPitch_deg * accelPitch_deg);
        } else {
            float cal_fx = fx - (hasDeskCal ? deskCalX : 0.0f);
            float cal_fy = fy - (hasDeskCal ? deskCalY : 0.0f);
            accelRoll_deg = asin(constrain(-cal_fy, -1.0f, 1.0f)) * (180.0f / M_PI);
            accelPitch_deg = asin(constrain(cal_fx, -1.0f, 1.0f)) * (180.0f / M_PI);
            totalTilt = sqrt(accelRoll_deg * accelRoll_deg + accelPitch_deg * accelPitch_deg);
        }
    }

    // 6-Axis Complementary Filter:
    // Fuses gyro angular rate with accelerometer tilt. During drill vibration (isVibrating),
    // gyro weight increases to 0.98 to reject high-frequency mechanical shock.
    if (!fusionInitialized) {
        fusedRoll = accelRoll_deg;
        fusedPitch = accelPitch_deg;
        fusionInitialized = true;
    } else {
        float dt = (now - lastSensor < 50) ? (now - lastSensor) / 1000.0f : 0.010f;
        float alphaGyro = isVibrating ? 0.98f : 0.94f;
        float rateRoll = gyroDPS_X;
        float ratePitch = isVerticalWallMode ? gyroDPS_Z : gyroDPS_Y;
        fusedRoll = alphaGyro * (fusedRoll + rateRoll * dt) + (1.0f - alphaGyro) * accelRoll_deg;
        fusedPitch = alphaGyro * (fusedPitch + ratePitch * dt) + (1.0f - alphaGyro) * accelPitch_deg;
    }

    float roll_deg = fusedRoll;
    float pitch_deg = fusedPitch;

    // 4. Touch Gestures & Calibration Triggers:
    // - Top-Right button: Cycle Target Presets (0°, 15°, 30°, 45°)
    // - Top-Left CAL button:
    //     * Short tap: Hands-off Desk Calibration (3s countdown -> save to Flash NVS)
    //     * Long press (>1.8s): Reset calibration to factory zero
    // - Center display area:
    //     * Double-tap: Also triggers Hands-off Desk Calibration
    //     * Single tap: Toggle temporary Relative Tare Mode
    //     * Hold (>2.2s): Enter Standby sleep
    static int32_t lastTouchX = 0;
    static int32_t lastTouchY = 0;
    static unsigned long lastMainTapTime = 0;

    int32_t tx = 0, ty = 0;
    bool isTouched = lcd.getTouch(&tx, &ty);
    if (isTouched) {
        lastTouchX = tx;
        lastTouchY = ty;
        lastActivityTime = now;
        if (isDisplayDimmed) {
            lcd.setBrightness(220);
            isDisplayDimmed = false;
        }

        if (!wasTouched) {
            touchStartTime = now;
            wasTouched = true;
        } else if (now - touchStartTime > 2200) {
            // Touch held for > 2.2 seconds:
            // If held on CAL button (top-left), don't enter standby - wait for release to trigger CAL reset!
            if (!(lastTouchX < 105 && lastTouchY < 75)) {
                enterStandby();
                wasTouched = false;
                return;
            }
        }
    } else if (!isTouched && wasTouched) {
        unsigned long pressDuration = now - touchStartTime;

        if (calibState == CALIB_COUNTDOWN) {
            // Any tap during the 3-second countdown cancels calibration
            calibState = CALIB_IDLE;
            Serial.println("[Calib] Calibration cancelled by user touch.");
        } else if (calibState == CALIB_IDLE && pressDuration > 30) {
            // 1. Top-Right: Target Preset Button
            if (lastTouchX > 135 && lastTouchY < 75) {
                currentPresetIdx = (currentPresetIdx + 1) % NUM_PRESETS;
                isTared = false;
                fusionInitialized = false;
                Serial.printf("[Touch] Preset cycled to: %.1f deg\n", TARGET_PRESETS[currentPresetIdx]);
            }
            // 2. Top-Left: CAL Button
            else if (lastTouchX < 105 && lastTouchY < 75) {
                if (pressDuration >= 1800) {
                    // Long press (>1.8s): Reset calibration back to factory zero
                    prefs.begin("bullseye", false);
                    prefs.remove("has_cal");
                    prefs.remove("cal_x");
                    prefs.remove("cal_y");
                    prefs.end();
                    hasDeskCal = false;
                    deskCalX = 0.0f;
                    deskCalY = 0.0f;
                    fusionInitialized = false;
                    calibState = CALIB_RESET;
                    calibStartTime = now;
                    Serial.println("[Calib] Desk Calibration erased -> Reverted to factory zero.");
                } else {
                    // Tap: Start Hands-Off Calibration Countdown
                    calibState = CALIB_COUNTDOWN;
                    calibStartTime = now;
                    Serial.println("[Calib] Calibration triggered -> 3s countdown started.");
                }
            }
            // 3. Main Screen Area: Double-Tap (Calibrate) or Single-Tap (Tare)
            else {
                if (now - lastMainTapTime < 450) {
                    // Double-tap detected -> trigger calibration countdown
                    calibState = CALIB_COUNTDOWN;
                    calibStartTime = now;
                    lastMainTapTime = 0;
                    Serial.println("[Calib] Double-tap detected -> Calibration countdown started.");
                } else {
                    lastMainTapTime = now;
                    if (isTared) {
                        isTared = false;
                        tareX = tareY = tareZ = 0.0f;
                        fusionInitialized = false;
                        Serial.println("[Touch] Tare cleared -> AUTO MODE.");
                    } else {
                        float tareMag = sqrt(fx * fx + fy * fy + fz * fz);
                        if (tareMag > 0.7f && tareMag < 1.3f) {
                            isTared = true;
                            tareX = fx;
                            tareY = fy;
                            tareZ = fz;
                            fusionInitialized = false;
                            Serial.printf("[Touch] Tared to: (%.2f, %.2f, %.2f) | mag: %.2f\n", tareX, tareY, tareZ, tareMag);
                        } else {
                            Serial.printf("[Touch] Tare rejected: invalid gravity vector mag (%.2f)\n", tareMag);
                        }
                    }
                }
            }
        }
        wasTouched = false;
    }

    // Calibration State Machine Processing
    if (calibState == CALIB_COUNTDOWN) {
        if (now - calibStartTime >= 3000) {
            calibState = CALIB_SAMPLING;
            calibStartTime = now;
            calibSumX = 0.0f;
            calibSumY = 0.0f;
            calibSampleCount = 0;
            Serial.println("[Calib] Sampling stationary gravity vectors for desk level...");
        }
    } else if (calibState == CALIB_SAMPLING) {
        calibSumX += fx;
        calibSumY += fy;
        calibSampleCount++;

        if (now - calibStartTime >= 1200 || calibSampleCount >= 100) {
            if (calibSampleCount > 10) {
                deskCalX = calibSumX / (float)calibSampleCount;
                deskCalY = calibSumY / (float)calibSampleCount;
                hasDeskCal = true;

                prefs.begin("bullseye", false);
                prefs.putBool("has_cal", true);
                prefs.putFloat("cal_x", deskCalX);
                prefs.putFloat("cal_y", deskCalY);
                prefs.end();

                fusionInitialized = false;
                Serial.printf("[Calib] SAVED to NVS: deskCalX=%.4f, deskCalY=%.4f (samples: %d)\n",
                              deskCalX, deskCalY, calibSampleCount);
            }
            calibState = CALIB_SAVED;
            calibStartTime = now;
        }
    } else if (calibState == CALIB_SAVED) {
        if (now - calibStartTime >= 1800) {
            calibState = CALIB_IDLE;
        }
    } else if (calibState == CALIB_RESET) {
        if (now - calibStartTime >= 1500) {
            calibState = CALIB_IDLE;
        }
    }

    // 5. Target Angle & Deviation Calculations
    float targetAngle = TARGET_PRESETS[currentPresetIdx];
    float displayAngle = totalTilt;
    float deviationError = totalTilt;

    float errorRoll = roll_deg;
    float errorPitch = pitch_deg;

    if (!isTared && targetAngle > 0.0f) {
        float currentInclination = abs(pitch_deg);
        displayAngle = currentInclination;
        errorPitch = currentInclination - targetAngle;
        errorRoll = roll_deg;
        deviationError = sqrt(errorPitch * errorPitch + errorRoll * errorRoll);
    }

    const float pxPerDeg = 7.0f;
    float bx = (float)CENTER_X + (errorRoll * pxPerDeg);
    float by = (float)CENTER_Y + (errorPitch * pxPerDeg);

    float evalTolerance = (!isTared && targetAngle > 0.0f) ? deviationError : totalTilt;

    // 6. Render Display at 50 FPS (every 20ms) with Fluid Viscosity Simulation
    if (now - lastUpdate >= 20) {
        lastUpdate = now;

        if (calibState != CALIB_IDLE) {
            drawCalibrationOverlay(now);
            return;
        }

        // Mineral spirit fluid damping filter
        const float fluidViscosity = 0.22f;
        smooth_bx += fluidViscosity * (bx - smooth_bx);
        smooth_by += fluidViscosity * (by - smooth_by);

        canvas.fillScreen(TFT_BLACK);

        if (!imuOk) {
            canvas.setTextColor(TFT_RED);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.drawString("IMU SENSOR ERROR", CENTER_X, 100, &fonts::Font4);
            canvas.setTextColor(TFT_WHITE);
            canvas.drawString("Check I2C Wiring (GPIO 6/7)", CENTER_X, 140, &fonts::Font2);
            canvas.pushSprite(0, 0);
            return;
        }

        // Bullseye Rings Color coding
        uint16_t ringColor = (evalTolerance < 0.5f) ? TFT_GREEN : 
                             (evalTolerance < 2.0f) ? TFT_YELLOW : canvas.color565(35, 60, 90);

        // Concentric guide rings
        canvas.drawCircle(CENTER_X, CENTER_Y, 95, canvas.color565(30, 40, 60));  // 10°
        canvas.drawCircle(CENTER_X, CENTER_Y, 65, canvas.color565(45, 60, 85));  // 5°
        canvas.drawCircle(CENTER_X, CENTER_Y, 35, canvas.color565(60, 80, 110)); // 2°
        canvas.drawCircle(CENTER_X, CENTER_Y, 15, ringColor);                     // 0.5°
        canvas.drawCircle(CENTER_X, CENTER_Y, 16, ringColor);

        // Crosshairs
        canvas.drawFastHLine(25, CENTER_Y, 190, canvas.color565(30, 45, 70));
        canvas.drawFastVLine(CENTER_X, 25, 190, canvas.color565(30, 45, 70));

        // Limit bubble to inside the max perimeter
        float dx = smooth_bx - (float)CENTER_X;
        float dy = smooth_by - (float)CENTER_Y;
        float dist = sqrt(dx * dx + dy * dy);
        float disp_bx = smooth_bx;
        float disp_by = smooth_by;
        if (dist > BUBBLE_MAX_DIST) {
            disp_bx = (float)CENTER_X + (dx / dist) * BUBBLE_MAX_DIST;
            disp_by = (float)CENTER_Y + (dy / dist) * BUBBLE_MAX_DIST;
        }

        uint16_t bubbleColor = (evalTolerance < 0.5f) ? TFT_GREEN : 
                               (evalTolerance < 2.0f) ? TFT_YELLOW : TFT_ORANGE;
        canvas.fillCircle((int32_t)disp_bx, (int32_t)disp_by, 11, bubbleColor);
        canvas.drawCircle((int32_t)disp_bx, (int32_t)disp_by, 11, TFT_WHITE);
        canvas.fillCircle((int32_t)disp_bx - 3, (int32_t)disp_by - 3, 3, TFT_WHITE);

        // Top Status Header
        canvas.setTextDatum(textdatum_t::top_center);
        if (isTared) {
            canvas.setTextColor(TFT_MAGENTA);
            canvas.drawString("TARE ACTIVE", CENTER_X, HEADER_Y, &fonts::Font0);
        } else if (targetAngle > 0.0f) {
            char targetHdr[24];
            snprintf(targetHdr, sizeof(targetHdr), "%.0f\xF8 BEVEL TARGET", targetAngle);
            canvas.setTextColor(canvas.color565(255, 190, 60));
            canvas.drawString(targetHdr, CENTER_X, HEADER_Y, &fonts::Font0);
        } else if (isVerticalWallMode) {
            canvas.setTextColor(canvas.color565(100, 180, 255));
            canvas.drawString("WALL DRILL", CENTER_X, HEADER_Y, &fonts::Font0);
        } else {
            canvas.setTextColor(canvas.color565(120, 220, 160));
            canvas.drawString("DESK LEVEL", CENTER_X, HEADER_Y, &fonts::Font0);
        }

        // Active Vibration Damping Indicator Badge
        if (isVibrating) {
            canvas.fillRoundRect(86, 36, 68, 14, 7, canvas.color565(180, 70, 0));
            canvas.setTextColor(TFT_WHITE);
            canvas.setTextDatum(textdatum_t::middle_center);
            canvas.drawString("⚡ VIB DAMP", CENTER_X, 43, &fonts::Font0);
        }

        // Top-Right Preset Button with Bevel Icon ⊿
        uint16_t btnBg = (targetAngle > 0.0f) ? 
                         (evalTolerance < 0.5f ? canvas.color565(0, 125, 55) : canvas.color565(60, 35, 90)) : 
                         canvas.color565(25, 38, 55);
        canvas.fillRoundRect(BTN_PRESET_X, BTN_PRESET_Y, BTN_PRESET_W, BTN_PRESET_H, BTN_PRESET_RADIUS, btnBg);
        canvas.drawRoundRect(BTN_PRESET_X, BTN_PRESET_Y, BTN_PRESET_W, BTN_PRESET_H, BTN_PRESET_RADIUS, canvas.color565(70, 95, 130));

        // Small bevel triangle icon ⊿
        uint16_t iconCol = (targetAngle > 0.0f) ? TFT_YELLOW : canvas.color565(160, 200, 240);
        canvas.fillTriangle(160, 42, 170, 42, 170, 32, iconCol);

        // Angle text in button with degree symbol
        char btnStr[12];
        if (targetAngle > 0.0f) {
            snprintf(btnStr, sizeof(btnStr), "%.0f\xF8", targetAngle);
        } else {
            snprintf(btnStr, sizeof(btnStr), "0\xF8");
        }
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString(btnStr, 175, 38, &fonts::Font0);

        // Top-Left CAL Button
        uint16_t calBg = hasDeskCal ? canvas.color565(15, 45, 30) : canvas.color565(25, 38, 55);
        uint16_t calBorder = hasDeskCal ? canvas.color565(40, 140, 75) : canvas.color565(70, 95, 130);
        canvas.fillRoundRect(BTN_CAL_X, BTN_CAL_Y, BTN_CAL_W, BTN_CAL_H, BTN_CAL_RADIUS, calBg);
        canvas.drawRoundRect(BTN_CAL_X, BTN_CAL_Y, BTN_CAL_W, BTN_CAL_H, BTN_CAL_RADIUS, calBorder);

        canvas.setTextDatum(textdatum_t::middle_center);
        if (hasDeskCal) {
            canvas.setTextColor(canvas.color565(140, 240, 160));
            canvas.drawString("CAL", BTN_CAL_X + 22, BTN_CAL_Y + 12, &fonts::Font0);
            canvas.fillCircle(BTN_CAL_X + 41, BTN_CAL_Y + 12, 3, TFT_GREEN);
        } else {
            canvas.setTextColor(canvas.color565(160, 200, 240));
            canvas.drawString("CAL", BTN_CAL_X + (BTN_CAL_W / 2), BTN_CAL_Y + 12, &fonts::Font0);
        }

        // Digital Angle Readout in Center Bottom with Vector Degree Circle °
        char angleStr[16];
        snprintf(angleStr, sizeof(angleStr), "%.1f", displayAngle);
        uint16_t angleColor = (evalTolerance < 0.5f) ? TFT_GREEN : 
                              (evalTolerance < 2.0f) ? TFT_YELLOW : TFT_WHITE;
        int numW = canvas.textWidth(angleStr, &fonts::Font4);
        int drawX = CENTER_X - 5;
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(angleColor);
        canvas.drawString(angleStr, drawX, 170, &fonts::Font4);

        // Vector degree sign circle °
        int degX = drawX + (numW / 2) + 6;
        int degY = 170 - 10;
        canvas.drawCircle(degX, degY, 3, angleColor);
        canvas.drawCircle(degX, degY, 4, angleColor);

        // Sub-badge: Deviation / Lock status
        if (targetAngle > 0.0f) {
            char subStr[32];
            if (evalTolerance < 0.5f) {
                snprintf(subStr, sizeof(subStr), "LOCKED %.1f\xF8", targetAngle);
                canvas.setTextColor(TFT_GREEN);
            } else {
                float err = displayAngle - targetAngle;
                snprintf(subStr, sizeof(subStr), "%+.1f\xF8 ERROR", err);
                canvas.setTextColor(canvas.color565(200, 180, 220));
            }
            canvas.drawString(subStr, CENTER_X, 194, &fonts::Font0);
        }

        // Bottom Battery Readout
        char batStr[32];
        if (cachedVBat < 3.0f) {
            snprintf(batStr, sizeof(batStr), "USB (NO BAT)");
            canvas.setTextColor(canvas.color565(120, 140, 160));
        } else if (cachedVBat >= 4.16f) {
            snprintf(batStr, sizeof(batStr), "%.2fV %d%% CHG", cachedVBat, cachedBatPct);
            canvas.setTextColor(canvas.color565(100, 220, 130)); // Green when charging / full
        } else {
            snprintf(batStr, sizeof(batStr), "%.2fV %d%%", cachedVBat, cachedBatPct);
            canvas.setTextColor(cachedBatPct <= 15 ? TFT_RED : canvas.color565(130, 150, 170));
        }
        canvas.setTextDatum(textdatum_t::bottom_center);
        canvas.drawString(batStr, CENTER_X, 225, &fonts::Font0);

        canvas.pushSprite(0, 0);
    }

    // Telemetry output over serial (1 Hz)
    if (now - lastSerial >= 1000) {
        lastSerial = now;
        Serial.printf("[Tele] Tilt: %5.1f deg | Roll: %+5.1f deg | Pitch: %+5.1f deg | fx:%+.2f fy:%+.2f fz:%+.2f | Mode: %s\n",
                      totalTilt, roll_deg, pitch_deg,
                      fx, fy, fz,
                      isTared ? "TARE" : (isVerticalWallMode ? "WALL" : "BENCH"));
    }
}
