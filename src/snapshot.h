// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// One frame's worth of what is in the drop, in screen pixels. Core 1 fills
// one after every simulation step; core 0 draws from the newest one while the
// next step runs. Three copies, so neither side ever waits on the other.
#pragma once
#include <stdint.h>
#include "evolve.h"

namespace cp {

enum SnapRole : uint8_t { ROLE_RIVAL, ROLE_HUNTER, ROLE_PET, ROLE_DROP, ROLE_CROWNED };

struct SnapCell {
    int16_t x, y;
    float   r, heading, wob, bop, eaten;
    float   ph[3];
    uint8_t cr, cg, cb, role;
};
constexpr int TENT = 16, TSEG = 8;  // jellyfish tentacles, points per tentacle
struct SnapPet {
    int         stage, nTent;
    evo::Traits shape;
    float       pulse, glow, blink, joy, shimmer, spin;  // pulse 0..1 contraction, glow 0..1 night light, shimmer after a stage change
    int16_t     tent[TENT][TSEG][2];
    uint8_t     sting[TENT];  // stinging threads kept from a hunting past
};
struct SnapAlgae {
    int16_t x, y;
    float   rot;
    uint8_t n, fed;
};

struct Snapshot {
    uint32_t  tMs;
    float     light;
    int       nCells;
    SnapCell  cells[16];
    int       nAlgae;
    SnapAlgae algae[64];
    int       nBits;
    int16_t   bits[48][2];
    float     tardX, tardY, tardDir;
    SnapPet   pet;
    char      hud[48], bigTime[8], info[40], leaning[40], banner[2][40];  // banner: a pop-up line or two (achievements)
    float     bannerAge;  // seconds it has been showing
    bool      desk, showBars;
    float     hp01, food01;
};

Snapshot*       snapWrite();    // a copy nobody is reading; fill all of it
void            snapPublish();  // make it the newest
const Snapshot* snapAcquire(uint32_t& seq);  // the newest; hold it until snapRelease
void            snapRelease();

}  // namespace cp
