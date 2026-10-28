// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Duck Pet's own rules, with no Arduino in them so they can be tested on a PC:
// how often a duckling is evil, who wins an evil fight, what prey is worth.
#pragma once
#include <stdint.h>

namespace duck {

int  clutchEvil(uint32_t& rng);       // round one: which clutch member is evil (0..7), at 4% a clutch; else -1
bool wildEvil(uint32_t& rng);         // a duckling that paddles in later: evil at 3%
bool normalBeatsEvil(uint32_t& rng);  // an evil fight, decided once on contact: the normal duck wins 12%

// what an evil fight does, once the winner is known: your duck is never the one that dies
constexpr float EVIL_MEAL = 25;  // an evil pet eats the duckling it beats (the cell's cannibal meal)
struct EvilOutcome {
    bool  evilDies, normalDies;  // eaten, and gone from the clutch for good
    float petEnergy;             // added to your duck's energy
};
EvilOutcome evilFightOutcome(bool upset, bool evilIsPet, bool normalIsPet);

enum Prey : uint8_t { BUG, FROG, FISH };
constexpr float WEED = 4, PEAS = 6;  // energy from duckweed and from the peas and oats you toss
float           preyEnergy(Prey p);
uint32_t        preyDelayMs(Prey p, float light01, uint32_t& rng);  // until the next one shows up; sooner in a bright room

// the duck save's extra flags (NVS duck1 / x): an evil pet, and which clutch member is evil
struct DuckFlags {
    bool    petEvil;
    uint8_t dropEvil;  // bit i: clutch member i
};
uint32_t  packFlags(DuckFlags f);
DuckFlags unpackFlags(uint32_t v);

}  // namespace duck
