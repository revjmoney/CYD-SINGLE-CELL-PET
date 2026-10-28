// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Duck Pet's pond: a clutch of glossy toy ducklings, one of them yours, plus
// the odd evil one, bugs, frogs, goldfish and a passing turtle. Same shape as
// the cell's world.h, so pond() can hand out either.
#pragma once
#include <stdint.h>
#include "evolve.h"

namespace cp {

void       duckBegin();
void       duckStep(uint32_t nowMs);
void       duckTap(int x, int y);
void       duckSave();
void       duckPause(bool on);
void       duckGenesis();  // a new clutch: round one, the duck is gone
evo::Save& duckData();
void       duckForce(float sun, float hunt, float plump);
void       duckDebug(char c);  // the cell's serial letters, plus E (nearest duckling turns evil), V (your duck turns evil), N (force night), X (prey and the turtle now)

}  // namespace cp
