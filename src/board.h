// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Hardware setup for the ESP32-2432S028R ("Cheap Yellow Display").
// Panel/touch pin map lifted from bdring's FluidDial CYD support.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

extern lgfx::LGFX_Device tft;

// returns "ILI9341" or "ST7789"
const char* boardInit(uint8_t rotation, uint32_t spiHz);
bool boardTouch(int& x, int& y);
