// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "board.h"

lgfx::LGFX_Device tft;

static lgfx::Bus_SPI       bus;
static lgfx::Light_PWM     light;
static lgfx::Panel_ILI9341 panel_ili9341;
static lgfx::Panel_ST7789  panel_st7789;
static lgfx::Touch_XPT2046 touch;

// Read one byte of a panel register, used to tell ILI9341 and ST7789 boards apart.
static uint32_t read_panel_reg(lgfx::IBus* b, int32_t pin_cs, uint_fast16_t cmd) {
    lgfx::pinMode(pin_cs, lgfx::pin_mode_t::output);
    b->beginTransaction();
    lgfx::gpio_hi(pin_cs);
    b->writeCommand(0, 8);
    b->wait();
    lgfx::gpio_lo(pin_cs);
    b->writeCommand(cmd, 8);
    b->beginRead(0);
    uint32_t res = b->readData(8) & 0xFF;
    b->endTransaction();
    lgfx::gpio_hi(pin_cs);
    return res;
}

const char* boardInit(uint8_t rotation, uint32_t spiHz) {
    {
        auto cfg        = bus.config();
        cfg.spi_host    = HSPI_HOST;
        cfg.spi_mode    = 0;
        cfg.freq_write  = spiHz;
        cfg.freq_read   = 16000000;
        cfg.use_lock    = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk    = GPIO_NUM_14;
        cfg.pin_mosi    = GPIO_NUM_13;
        cfg.pin_miso    = GPIO_NUM_12;
        cfg.pin_dc      = GPIO_NUM_2;
        bus.config(cfg);
        bus.init();
    }
    {
        auto cfg        = light.config();
        cfg.pin_bl      = GPIO_NUM_21;
        cfg.freq        = 12000;
        cfg.pwm_channel = 7;
        light.config(cfg);
    }

    // FluidDial's probe: register 0xDA reads 0 on the ILI9341 boards
    bool         ili  = read_panel_reg(&bus, GPIO_NUM_15, 0xDA) == 0;
    lgfx::Panel_Device* p = ili ? (lgfx::Panel_Device*)&panel_ili9341 : (lgfx::Panel_Device*)&panel_st7789;
    {
        auto cfg            = p->config();
        cfg.pin_cs          = GPIO_NUM_15;
        cfg.pin_rst         = -1;
        cfg.panel_width     = 240;
        cfg.panel_height    = 320;
        cfg.offset_rotation = 0;
        cfg.readable        = true;
        cfg.bus_shared      = false;
        p->config(cfg);
        p->setBus(&bus);
        p->setLight(&light);
    }
    {
        auto cfg            = touch.config();
        cfg.x_min           = 300;
        cfg.x_max           = 3900;
        cfg.y_min           = 3700;
        cfg.y_max           = 200;
        cfg.pin_int         = -1;
        cfg.bus_shared      = false;
        cfg.spi_host        = -1;  // XPT2046 sits on its own pins, bit-banged
        cfg.pin_sclk        = GPIO_NUM_25;
        cfg.pin_mosi        = GPIO_NUM_32;
        cfg.pin_miso        = GPIO_NUM_39;
        cfg.pin_cs          = GPIO_NUM_33;
        cfg.offset_rotation = 2;
        touch.config(cfg);
        p->setTouch(&touch);
    }
    tft.setPanel(p);
    tft.init();
    tft.setRotation(rotation);
    tft.setBrightness(255);
    tft.setColorDepth(16);
    return ili ? "ILI9341" : "ST7789";
}

// The panel's touch comes out rotated 180 degrees against the picture, in every
// orientation (landscape measured by tapping the corners; portrait was first
// fixed for up/down only and left the game pad's left and right swapped).
// Corrected here so every screen gets true coordinates.
bool boardTouch(int& x, int& y) {
    lgfx::touch_point_t tp;
    if (!tft.getTouch(&tp, 1)) return false;
    int w = tft.width(), h = tft.height();
    x     = w - 1 - tp.x;
    y     = h - 1 - tp.y;
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    return true;
}
