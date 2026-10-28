// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The evolution rules, with no Arduino in them so they can be tested on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace evo {

constexpr uint32_t STAGE_SECS = 129600;  // 36 hours of evolving time per stage
constexpr int      STAGES     = 5;

// evolving time: counts only while running and not paused
struct Clock {
    uint32_t evolveSecs = 0;
    bool     paused     = false;
    uint32_t lastMs     = 0;
    uint16_t carryMs    = 0;
    bool     started    = false;
};
uint32_t tick(Clock& c, uint32_t nowMs);  // seconds added by this call
int      stageFor(uint32_t evolveSecs);   // 1..5
float    stageProgress(uint32_t evolveSecs);

// the light sensor on the front (GPIO 34), read at 0 dB, the most sensitive range:
// 0 in a lit room, rising to about LIGHT_DARK when nearly dark. It can't see a lamp
// being dimmed, only lights on or nearly off.
constexpr int LIGHT_DARK = 2200;
float         roomLight(int raw);  // 0 dark .. 1 lit

// three body leanings, each -1..1: + is bright / hunter / plump, - is glow / grazer / sleek
struct Traits {
    float sun, hunt, plump;
};
// a round-one cell's inborn leanings
struct Genome {
    Traits  lean;
    uint8_t hue;
    float   size, speed;
};
Genome randomGenome(uint32_t& rng);

// the care tallies kept while it evolves
struct Tally {
    float    lightSum;
    uint32_t lightN;
    uint32_t fights, wins;
    uint32_t fedMeals, wildMeals;
};
void   addLight(Tally& t, float light01, int localHour, bool clockValid);  // daytime only (07:00 to 21:59)
void   addFight(Tally& t, bool won);
void   addMeal(Tally& t, bool fedByPerson);
Traits tallyTraits(const Tally& t);

// care locked into the body at each stage change
struct Body {
    Traits locked[STAGES];
    int    stage;
};
void   lockStage(Body& b, const Traits& now);
Traits shapeOf(const Body& b, const Traits& live);  // later stages weigh more; unlocked ones use live
void   leaningText(const Traits& t, char* out, int n);

// round one: the first cell to reach WIN_FITNESS becomes the pet
constexpr float WIN_FITNESS = 40;
float fitness(uint32_t meals, uint32_t wins, float hpFrac);

// everything kept across reboots, checked with a CRC so a half-written save is never trusted
constexpr int DROP = 8;  // round-one cells
#define EVO_SAVE_BODY                                                                  \
    uint32_t evolveSecs;                                                               \
    uint8_t  paused;                                                                   \
    uint8_t  havePet;                                                                  \
    Tally    tally;                                                                    \
    Body     body;                                                                     \
    Genome   pet;                                                                      \
    float    hp01, energy;                                                             \
    uint32_t kills;                                                                    \
    int64_t  born;                                                                     \
    Genome   drop[DROP];                                                               \
    uint8_t  dropAlive[DROP];                                                          \
    float    dropFit[DROP];                                                            \
    uint16_t dropMeals[DROP], dropWins[DROP]; /* round one's progress, kept on reboot */
struct Save {
    uint16_t magic   = 0xCE11;
    uint16_t version = 2;
    EVO_SAVE_BODY
    uint32_t quipOrder;  // the silly achievements: where it is in its shuffled pool
    uint16_t quipPos;
    uint8_t  quipPool;
    uint32_t quipDone;   // event lines already shown, one bit each
    uint32_t crc;
};
struct SaveV1 {  // the first save layout, read once and brought forward
    uint16_t magic, version;
    EVO_SAVE_BODY
    uint32_t crc;
};
bool migrateV1(const void* bytes, size_t n, Save& out);  // false if it isn't a v1 save
Save freshSave(uint32_t seed);  // a new drop: round one, no pet yet
void seal(Save& s);
bool valid(const Save& s, size_t bytesRead);
Save upgradeOld(uint8_t lvl, float xp, uint32_t kills, float energy, int64_t born);  // the skeleton's "pet" keys

// where the saved pet comes from at boot. The CYD Demoscene "pet" keys are
// only ever used when this firmware has never saved (no pet2 at all).
enum class LoadFrom : uint8_t { Pet2, Migrated, Legacy, Fresh };
LoadFrom planLoad(size_t pet2Bytes, bool pet2Valid, bool migratedV1, bool legacyKeys);
bool     roundOneEmpty(const Save& s);  // no pet, and no contender left

// animation time for drawing: wraps every ~17 minutes (one tiny phase pop) so
// float phases stay fine-grained however long the board stays on
constexpr uint32_t ANIM_WRAP_MS = 1u << 20;
uint32_t           animMs(uint32_t ms);

}  // namespace evo
