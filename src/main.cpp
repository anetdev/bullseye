#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <QMI8658.h>
#include "pin_config.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "bullseye"
#endif
#include "logo_data.h"
#include "LGFX_ESP32S3_GC9A01.h"

// Bullseye — Smart Digital Bullseye Level & Precision Angle Guide
// Real-Time 3D Bullseye Spirit Level with 6-Axis Gyro Fusion & Silicon LPF

// --- Display & Layout Constants ---
constexpr int CENTER_X          = 120;
constexpr int CENTER_Y          = 120;
constexpr float BUBBLE_MAX_DIST = 85.0f;
constexpr int HEADER_Y          = 20;
constexpr int BTN_PRESET_X      = 152;
constexpr int BTN_PRESET_Y      = 26;
constexpr int BTN_PRESET_W      = 60;
constexpr int BTN_PRESET_H      = 24;
constexpr int BTN_PRESET_RADIUS = 12;

LGFX_Waveshare128 lcd;
LGFX_Sprite canvas(&lcd);
QMI8658 qmi;
WiFiManager wm;

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

// Tare offsets (in g's)
float tareX = 0.0f;
float tareY = 0.0f;
float tareZ = 0.0f;
bool isTared = false;

// Quick Target Angle Presets (in degrees)
const float TARGET_PRESETS[] = { 0.0f, 15.0f, 30.0f, 45.0f };
const int NUM_PRESETS = sizeof(TARGET_PRESETS) / sizeof(TARGET_PRESETS[0]);
int currentPresetIdx = 0; // 0 = 0.0° (Level / Plumb)

// OTA status
bool isUpdatingOTA = false;
int otaProgress = 0;

float readBatteryVoltage() {
    uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
    return (mv * 2.0f) / 1000.0f;
}

int getBatteryPercent(float v) {
    if (v >= 4.15f) return 100;
    if (v <= 3.30f) return 0;
    return (int)((v - 3.30f) / (4.15f - 3.30f) * 100.0f);
}

void enterStandby() {
    isStandby = true;
    canvas.fillScreen(TFT_BLACK);
    canvas.pushSprite(0, 0);
    lcd.sleep();
    setCpuFrequencyMhz(80); // Drop CPU clock to save battery power
    Serial.println("[Power] Standby mode activated: Display OFF, Touch & Motion active.");
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

void setupNetwork() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true); // Enable 802.11 Modem Sleep: saves ~85% Wi-Fi idle power while keeping OTA active
    WiFi.setAutoReconnect(true);

    #ifdef WIFI_SSID
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 4500) {
        delay(150);
        Serial.print(".");
    }
    #endif

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Offline mode. Spirit level running immediately.");
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() {
        isUpdatingOTA = true;
        canvas.fillScreen(TFT_BLACK);
        canvas.setTextColor(TFT_WHITE);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("Bullseye OTA", CENTER_X, 90, &fonts::Font4);
        canvas.drawString("Updating Firmware...", CENTER_X, 125, &fonts::Font2);
        canvas.pushSprite(0, 0);
        Serial.println("[OTA] Update started.");
    });

    ArduinoOTA.onEnd([]() {
        canvas.fillScreen(TFT_BLACK);
        canvas.setTextColor(TFT_GREEN);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("Update Complete!", CENTER_X, 110, &fonts::Font4);
        canvas.drawString("Rebooting...", CENTER_X, 145, &fonts::Font2);
        canvas.pushSprite(0, 0);
        Serial.println("[OTA] Update complete. Rebooting.");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        otaProgress = (total > 0) ? (int)(100.0f * progress / total) : 0;
        
        canvas.fillScreen(TFT_BLACK);
        canvas.setTextColor(TFT_CYAN);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("Flashing Wireless...", CENTER_X, 85, &fonts::Font2);
        
        char pctStr[16];
        snprintf(pctStr, sizeof(pctStr), "%d%%", otaProgress);
        canvas.drawString(pctStr, CENTER_X, 120, &fonts::Font4);

        canvas.drawRect(40, 150, 160, 14, TFT_WHITE);
        int barW = (int)(156.0f * (otaProgress / 100.0f));
        canvas.fillRect(42, 152, barW, 10, TFT_GREEN);
        canvas.pushSprite(0, 0);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* errDesc = "Unknown Error";
        if (error == OTA_AUTH_ERROR) errDesc = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) errDesc = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) errDesc = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) errDesc = "Receive Failed";
        else if (error == OTA_END_ERROR) errDesc = "End Failed";

        Serial.printf("[OTA] Error[%u]: %s\n", error, errDesc);
        isUpdatingOTA = false;
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] ArduinoOTA service initialized.");
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(PIN_BATTERY_ADC, INPUT);
    delay(200);

    Serial.println("\n========================================");
    Serial.println("              Bullseye Smart Level      ");
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
    canvas.drawString("Bullseye", CENTER_X, 196, &fonts::Font2);
    canvas.setTextColor(TFT_CYAN);
    canvas.drawString("Connecting & Calibrating...", CENTER_X, 218, &fonts::Font0);
    canvas.pushSprite(0, 0);

    // 3. Setup Wi-Fi & OTA
    setupNetwork();

    if (WiFi.status() == WL_CONNECTED) {
        canvas.fillRect(20, 210, 200, 20, TFT_BLACK);
        canvas.setTextColor(TFT_GREEN);
        canvas.drawString(WiFi.localIP().toString().c_str(), CENTER_X, 218, &fonts::Font0);
        canvas.pushSprite(0, 0);
    }

    // 4. Initialize Shared I2C Bus & QMI8658 6-Axis IMU
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

        // Quick stationary gyro bias sampling (20 readings)
        float sumGx = 0, sumGy = 0, sumGz = 0;
        for (int i = 0; i < 20; i++) {
            float gx = 0, gy = 0, gz = 0;
            qmi.readGyroDPS(gx, gy, gz);
            sumGx += gx;
            sumGy += gy;
            sumGz += gz;
            delay(5);
        }
        gyroBiasX = sumGx / 20.0f;
        gyroBiasY = sumGy / 20.0f;
        gyroBiasZ = sumGz / 20.0f;

        Serial.printf("[IMU] QMI8658 6-Axis online. Hardware LPF 0x55 active. Gyro bias: (%.2f, %.2f, %.2f) dps\n",
                      gyroBiasX, gyroBiasY, gyroBiasZ);
    } else {
        Serial.println("[IMU] ERROR: QMI8658 not detected on I2C bus!");
    }

    lastActivityTime = millis();
    delay(800);
}

void loop() {
    // 1. Background OTA requests
    ArduinoOTA.handle();
    if (isUpdatingOTA) return;

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

    // If in Standby: poll touch and motion at low rate (every 50ms) to save battery
    if (isStandby) {
        delay(50);
        ArduinoOTA.handle();

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
    if (isVerticalWallMode) {
        if (abs(fz) > 0.65f && abs(fx) < 0.65f) {
            isVerticalWallMode = false;
        }
    } else {
        if (abs(fx) > 0.65f && abs(fz) < 0.65f) {
            isVerticalWallMode = true;
        }
    }

    float accelRoll_deg = 0.0f;
    float accelPitch_deg = 0.0f;
    float totalTilt = 0.0f;

    if (isTared) {
        float dot = fx * tareX + fy * tareY + fz * tareZ;
        totalTilt = acos(constrain(dot, -1.0f, 1.0f)) * (180.0f / M_PI);

        float dx = fx - tareX;
        float dy = fy - tareY;
        float dz = fz - tareZ;

        accelRoll_deg = asin(constrain(-dy, -1.0f, 1.0f)) * (180.0f / M_PI);
        if (isVerticalWallMode) {
            accelPitch_deg = asin(constrain(-dz, -1.0f, 1.0f)) * (180.0f / M_PI);
        } else {
            accelPitch_deg = asin(constrain(dx, -1.0f, 1.0f)) * (180.0f / M_PI);
        }
    } else {
        if (isVerticalWallMode) {
            accelRoll_deg = asin(constrain(-fy, -1.0f, 1.0f)) * (180.0f / M_PI);
            accelPitch_deg = asin(constrain(-fz, -1.0f, 1.0f)) * (180.0f / M_PI);
            totalTilt = sqrt(accelRoll_deg * accelRoll_deg + accelPitch_deg * accelPitch_deg);
        } else {
            accelRoll_deg = asin(constrain(-fy, -1.0f, 1.0f)) * (180.0f / M_PI);
            accelPitch_deg = asin(constrain(fx, -1.0f, 1.0f)) * (180.0f / M_PI);
            totalTilt = sqrt(accelRoll_deg * accelRoll_deg + accelPitch_deg * accelPitch_deg);
        }
    }

    float roll_deg = accelRoll_deg;
    float pitch_deg = accelPitch_deg;

    // 4. Touch Gestures:
    // Top-right button: Cycle Target Presets
    // Tap elsewhere: Toggle Relative Tare Mode
    static int32_t lastTouchX = 0;
    static int32_t lastTouchY = 0;

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
            // Touch held for > 2.2 seconds -> Put display to sleep manually!
            enterStandby();
            wasTouched = false;
            return;
        }
    } else if (!isTouched && wasTouched) {
        unsigned long pressDuration = now - touchStartTime;
        if (pressDuration > 30 && pressDuration <= 2200) {
            if (lastTouchX > 135 && lastTouchY < 75) {
                currentPresetIdx = (currentPresetIdx + 1) % NUM_PRESETS;
                isTared = false;
                Serial.printf("[Touch] Preset cycled to: %.1f deg\n", TARGET_PRESETS[currentPresetIdx]);
            } else {
                if (isTared) {
                    isTared = false;
                    tareX = tareY = tareZ = 0.0f;
                    Serial.println("[Touch] Tare cleared -> AUTO MODE.");
                } else {
                    float tareMag = sqrt(fx * fx + fy * fy + fz * fz);
                    if (tareMag > 0.7f && tareMag < 1.3f) {
                        isTared = true;
                        tareX = fx;
                        tareY = fy;
                        tareZ = fz;
                        Serial.printf("[Touch] Tared to: (%.2f, %.2f, %.2f) | mag: %.2f\n", tareX, tareY, tareZ, tareMag);
                    } else {
                        Serial.printf("[Touch] Tare rejected: invalid gravity vector mag (%.2f)\n", tareMag);
                    }
                }
            }
        }
        wasTouched = false;
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

        // Wi-Fi Connection Dot at Top Left (42, 38)
        if (WiFi.status() == WL_CONNECTED) {
            canvas.fillCircle(42, 38, 4, TFT_GREEN);
        } else if (wm.getConfigPortalActive()) {
            canvas.fillCircle(42, 38, 4, TFT_ORANGE);
        } else {
            canvas.drawCircle(42, 38, 4, TFT_DARKGRAY);
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
        char batStr[24];
        snprintf(batStr, sizeof(batStr), "%.2fV %d%%", cachedVBat, cachedBatPct);
        canvas.setTextDatum(textdatum_t::bottom_center);
        canvas.setTextColor(canvas.color565(130, 150, 170));
        canvas.drawString(batStr, CENTER_X, 226, &fonts::Font0);

        canvas.pushSprite(0, 0);
    }

    // Telemetry output over serial (1 Hz)
    if (now - lastSerial >= 1000) {
        lastSerial = now;
        Serial.printf("[Tele] Tilt: %5.1f deg | Roll: %+5.1f deg | Pitch: %+5.1f deg | Vib: %s | Dim: %s | Mode: %s\n",
                      totalTilt, roll_deg, pitch_deg,
                      isVibrating ? "YES" : "NO",
                      isDisplayDimmed ? "YES" : "NO",
                      isTared ? "TARE" : (isVerticalWallMode ? "WALL" : "BENCH"));
    }
}
