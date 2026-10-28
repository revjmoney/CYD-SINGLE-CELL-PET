// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The drop: everything that lives in it, round one, the pet's evolution and
// its save. Runs on core 1; each step ends by publishing a Snapshot.
#pragma once
#include <stdint.h>
#include "evolve.h"

namespace cp {

void       worldBegin();
void       worldStep(uint32_t nowMs);
void       worldTap(int x, int y);  // screen pixels
void       worldSave();
void       worldPause(bool on);  // EVOLVING: PAUSED / ON
void       worldGenesis();       // a new drop: round one, the pet is gone
evo::Save& worldData();
void       worldForce(float sun, float hunt, float plump);  // serial F: show these traits (testing only, not saved)
void       worldDebug(char c);  // serial: + six hours, n next stage, t traits, r new drop, w fast round one, h hold round one, q next silly achievement now, Z freeze for filming, . one 50 ms step while frozen, K pet to the middle

}  // namespace cp
