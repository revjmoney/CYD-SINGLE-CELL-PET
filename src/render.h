// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Draws the drop at the panel's full 320x240, one 16-line strip at a time,
// straight from the newest Snapshot. No full frame is ever kept in memory.
#pragma once
#include <stdint.h>
#include "snapshot.h"

namespace cp {

void renderInit();         // call once, before WiFi, so its buffers come first
void renderTaskStart();    // the drawing loop, pinned to core 0
void renderPause(bool on);  // on: returns once no strip is in flight (for the settings screen)
void renderShot();         // serial screenshot: SHOT, u16 w, u16 h, RGB888 rows
void renderStats(uint32_t& frames, uint32_t& drawUs);  // since the last call

}  // namespace cp
