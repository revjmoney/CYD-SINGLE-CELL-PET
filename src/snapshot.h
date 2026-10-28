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

// Duck Pet's pond (kind 1)
enum DuckRole : uint8_t { DUCKLING, DUCK_PET, DUCK_CROWNED };
enum DuckLook : uint8_t { LOOK_SLEEP = 1, LOOK_STARTLED = 2, LOOK_QUACK = 4, LOOK_PREEN = 8 };
struct SnapDuck {
    int16_t x, y;
    float   size, face, bob, dabble, flap, alpha, calm;  // face: +1 right, -1 left (blends while turning)
    uint8_t role, look, stage, evil;
};
struct SnapPrey {
    int16_t x, y;
    uint8_t type;  // duck::Prey
    float   phase, depth;  // depth 0 at the surface, 1 deep (a goldfish fades)
};
struct SnapSplat {
    int16_t x, y;
    uint8_t red;  // 0: a dark feather instead
    float   life;
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
    // Duck Pet only
    uint8_t   kind;  // 0 Cell Pet, 1 Duck Pet
    int       nDucks, nPrey, nFood, nSplat;
    SnapDuck  ducks[16];
    SnapPrey  prey[8];
    int16_t   food[32][2];
    uint8_t   foodType[32];  // 0 duckweed, 1 peas and oats
    SnapSplat splat[64];
    float     turtleX, turtleY, turtleDir;
    bool      splatterRed;
};

Snapshot*       snapWrite();    // a copy nobody is reading; fill all of it
void            snapPublish();  // make it the newest
const Snapshot* snapAcquire(uint32_t& seq);  // the newest; hold it until snapRelease
void            snapRelease();

}  // namespace cp
