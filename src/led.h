// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The RGB LED on the back of the CYD (GPIO 4 red, 16 green, 17 blue; lit when
// pulled low): short flashes when the pet does something.
#pragma once
#include <stdint.h>

namespace cp {

void ledInit();
void ledFlash(bool r, bool g, bool b, uint16_t ms);  // replaces any flash still showing
void ledUpdate();  // call every loop: turns it off when the time is up

}  // namespace cp
