// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "led.h"
#include <Arduino.h>

namespace cp {

static const uint8_t pins[3] = {4, 16, 17};
static uint32_t      offAt;

static void show(bool r, bool g, bool b) {
    digitalWrite(pins[0], !r), digitalWrite(pins[1], !g), digitalWrite(pins[2], !b);
}

void ledInit() {
    for (auto p : pins) pinMode(p, OUTPUT);
    show(false, false, false);
}

void ledFlash(bool r, bool g, bool b, uint16_t ms) {
    show(r, g, b);
    offAt = millis() + ms;
    if (!offAt) offAt = 1;
}

void ledUpdate() {
    if (offAt && (int32_t)(millis() - offAt) >= 0) {
        offAt = 0;
        show(false, false, false);
    }
}

}  // namespace cp
