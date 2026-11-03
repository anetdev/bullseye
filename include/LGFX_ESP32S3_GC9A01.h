#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pin_config.h"

class LGFX_Waveshare128 : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
    lgfx::Touch_CST816S _touch_instance;

public:
    LGFX_Waveshare128(void)
    {
        // 1. Configure SPI Bus for GC9A01 Display
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;  // 40MHz
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = true;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            cfg.pin_sclk = PIN_LCD_SCLK; // 10
            cfg.pin_mosi = PIN_LCD_MOSI; // 11
            cfg.pin_miso = -1;
            cfg.pin_dc   = PIN_LCD_DC;   // 8

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // 2. Configure GC9A01 Display Panel
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = PIN_LCD_CS;   // 9
            cfg.pin_rst  = PIN_LCD_RST;  // 14
            cfg.pin_busy = -1;

            cfg.panel_width  = LCD_WIDTH;  // 240
            cfg.panel_height = LCD_HEIGHT; // 240
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 0;
            cfg.readable     = false;
            cfg.invert       = true;  // GC9A01 IPS panel inverted colors
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = false;

            _panel_instance.config(cfg);
        }

        // 3. Configure Backlight (PWM)
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = PIN_LCD_BL; // GPIO 2
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        // 4. Configure CST816S Capacitive Touch Controller
        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = LCD_WIDTH;
            cfg.y_min = 0;
            cfg.y_max = LCD_HEIGHT;
            cfg.pin_int = PIN_TOUCH_INT; // 5
            cfg.pin_rst = PIN_TOUCH_RST; // 13
            cfg.bus_shared = true;       // Shared with IMU on I2C
            cfg.offset_rotation = 0;

            cfg.i2c_port = I2C_NUM_0;
            cfg.i2c_addr = TOUCH_I2C_ADDR; // 0x15
            cfg.pin_sda  = PIN_I2C_SDA;    // 6
            cfg.pin_scl  = PIN_I2C_SCL;    // 7
            cfg.freq     = 400000;

            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
