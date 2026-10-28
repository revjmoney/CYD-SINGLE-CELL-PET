// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "duckrules.h"

namespace duck {

static uint32_t next(uint32_t& s) {
    if (!s) s = 0x6D2B79F5u;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
}
static bool chance(uint32_t& rng, uint32_t perMillion) { return next(rng) % 1000000 < perMillion; }

int clutchEvil(uint32_t& rng) {
    if (!chance(rng, 40000)) return -1;  // 4% of clutches: about 3% of crowned pets end up evil
    return next(rng) % 8;
}
bool wildEvil(uint32_t& rng) { return chance(rng, 30000); }
bool normalBeatsEvil(uint32_t& rng) { return chance(rng, 120000); }

EvilOutcome evilFightOutcome(bool upset, bool evilIsPet, bool normalIsPet) {
    if (upset) return {!evilIsPet, false, 0};       // your evil duck flees hurt instead
    if (normalIsPet) return {false, false, -20};    // your duck flees hurt
    return {false, true, evilIsPet ? EVIL_MEAL : 0};
}

float preyEnergy(Prey p) { return p == FROG ? 15 : p == FISH ? 10 : 7; }

uint32_t preyDelayMs(Prey p, float light01, uint32_t& rng) {
    uint32_t lo = p == BUG ? 30000 : p == FROG ? 180000 : 120000, hi = p == BUG ? 90000 : p == FROG ? 360000 : 300000;
    float    base = lo + next(rng) % (hi - lo + 1);
    if (light01 < 0) light01 = 0;
    if (light01 > 1) light01 = 1;
    return (uint32_t)(base / (0.6f + 0.8f * light01));
}

uint32_t  packFlags(DuckFlags f) { return (f.petEvil ? 1u : 0u) | (uint32_t)f.dropEvil << 8; }
DuckFlags unpackFlags(uint32_t v) { return {(v & 1) != 0, (uint8_t)(v >> 8)}; }

}  // namespace duck
