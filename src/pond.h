// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Which pet lives in the pond: Cell Pet's drop or Duck Pet's pond. The main
// loop and the settings only ever talk to pond(), picked once at boot.
#pragma once
#include <stdint.h>
#include "evolve.h"

namespace cp {

struct Pond {
    void (*begin)();
    void (*step)(uint32_t nowMs);
    void (*tap)(int x, int y);
    void (*save)();
    evo::Save& (*data)();
    void (*debug)(char c);
    void (*force)(float sun, float hunt, float plump);
    void (*pause)(bool on);
    void (*genesis)();
};
extern const Pond cellPond, duckPond;
const Pond&       pond();

}  // namespace cp
