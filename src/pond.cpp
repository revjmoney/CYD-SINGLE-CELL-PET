// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "pond.h"
#include "duckworld.h"
#include "pet.h"
#include "world.h"

namespace cp {

const Pond cellPond = {worldBegin, worldStep, worldTap, worldSave, worldData, worldDebug, worldForce, worldPause, worldGenesis};
const Pond duckPond = {duckBegin, duckStep, duckTap, duckSave, duckData, duckDebug, duckForce, duckPause, duckGenesis};

const Pond& pond() { return petKind == Kind::Duck ? duckPond : cellPond; }

}  // namespace cp
