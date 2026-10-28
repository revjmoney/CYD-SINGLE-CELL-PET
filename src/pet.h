// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the pet, the screen pusher and the settings share.
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace cp {

constexpr int SW = 320, SH = 240;  // panel, landscape
constexpr int LW = 160, LH = 120;  // half size: the lamp table
constexpr int STRIP = 16;          // lines per DMA strip
constexpr int LSIZE = LW * LH;     // one colour plane

// which pet this board runs: picked at boot (PET_DEFAULT, or the PET setting when built with PET_SWITCH)
enum class Kind : uint8_t { Cell, Duck };
extern Kind petKind;
extern bool splatterRed;  // Duck Pet: red splatter (the default) or dark feathers only

extern const uint8_t* font16;  // IBM VGA 8x16

static inline uint16_t rgb(int r, int g, int b) {  // byte-swapped RGB565, ready for the panel
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
}

// the finger, in screen pixels
struct TouchState {
    bool     down = false, pressed = false;  // pressed: set on touch-down, cleared once the world has it
    int      x = 0, y = 0;
    uint32_t lastMs = 0;
};
extern TouchState touch;
extern bool       deskClock;  // big time, date and weather over the water

}  // namespace cp
