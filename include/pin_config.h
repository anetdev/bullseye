#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.28 Pinout Definition

// --- Display (GC9A01 SPI) ---
#define PIN_LCD_MOSI       11
#define PIN_LCD_SCLK       10
#define PIN_LCD_CS          9
#define PIN_LCD_DC          8
#define PIN_LCD_RST        14
#define PIN_LCD_BL          2

// --- Shared I2C Bus ---
#define PIN_I2C_SDA         6
#define PIN_I2C_SCL         7

// --- CST816S Touch Controller ---
#define PIN_TOUCH_INT       5
#define PIN_TOUCH_RST      13
#define TOUCH_I2C_ADDR   0x15

// --- QMI8658 6-Axis IMU ---
#define PIN_IMU_INT1       46
#define PIN_IMU_INT2       45
#define IMU_I2C_ADDR     0x6B

// --- Power & System ---
#define PIN_BATTERY_ADC     1

// Display dimensions
#define LCD_WIDTH         240
#define LCD_HEIGHT        240
