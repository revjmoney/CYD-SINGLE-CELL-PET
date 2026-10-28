// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "evolve.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace evo {

uint32_t tick(Clock& c, uint32_t nowMs) {
    if (!c.started) {
        c.started = true;
        c.lastMs  = nowMs;
        return 0;
    }
    uint32_t dt = nowMs - c.lastMs;  // unsigned: fine across the 49-day millis() wrap
    c.lastMs    = nowMs;
    if (c.paused) return 0;
    uint32_t ms = dt + c.carryMs, secs = ms / 1000;
    c.carryMs   = ms % 1000;
    c.evolveSecs += secs;
    return secs;
}

int stageFor(uint32_t evolveSecs) {
    uint32_t s = 1 + evolveSecs / STAGE_SECS;
    return s > (uint32_t)STAGES ? STAGES : (int)s;
}

float stageProgress(uint32_t evolveSecs) {
    if (stageFor(evolveSecs) == STAGES) return 1;
    return (evolveSecs % STAGE_SECS) / (float)STAGE_SECS;
}

static float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

static uint32_t xorshift(uint32_t& s) {
    if (!s) s = 0x9E3779B9u;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
}
static float unit(uint32_t& s) { return (xorshift(s) & 0xFFFFFF) / (float)0xFFFFFF; }  // 0..1

Genome randomGenome(uint32_t& rng) {
    Genome g;
    g.lean  = {unit(rng) * 2 - 1, unit(rng) * 2 - 1, unit(rng) * 2 - 1};
    g.hue   = xorshift(rng) & 0xFF;
    g.size  = 0.8f + 0.4f * unit(rng);
    g.speed = 0.8f + 0.4f * unit(rng);
    return g;
}

void addLight(Tally& t, float light01, int localHour, bool clockValid) {
    if (clockValid && (localHour < 7 || localHour > 21)) return;
    t.lightSum += light01;
    t.lightN++;
}
void addFight(Tally& t, bool won) {
    t.fights++;
    if (won) t.wins++;
}
void addMeal(Tally& t, bool fedByPerson) { (fedByPerson ? t.fedMeals : t.wildMeals)++; }

Traits tallyTraits(const Tally& t) {
    float light = t.lightN ? t.lightSum / t.lightN : 0.5f;
    float wins  = t.fights ? (float)t.wins / t.fights : 0;
    float sure  = t.fights < 20 ? t.fights / 20.0f : 1;  // a few fights don't make a hunter
    uint32_t meals = t.fedMeals + t.wildMeals;
    return {clamp1(2 * light - 1), clamp1(wins * sure * 2 - 1), meals ? clamp1(2.0f * t.fedMeals / meals - 1) : 0};
}

void lockStage(Body& b, const Traits& now) {
    if (b.stage < 1) b.stage = 1;
    if (b.stage > STAGES) return;
    b.locked[b.stage - 1] = now;
    b.stage++;
}

Traits shapeOf(const Body& b, const Traits& live) {
    Traits s{0, 0, 0};
    float  wsum = 0;
    for (int i = 0; i < STAGES; i++) {
        const Traits& t = i < b.stage - 1 ? b.locked[i] : live;
        float w = i + 1.0f;
        s.sun += t.sun * w, s.hunt += t.hunt * w, s.plump += t.plump * w;
        wsum += w;
        if (i >= b.stage - 1) break;  // the live traits count once, as the current stage
    }
    return {s.sun / wsum, s.hunt / wsum, s.plump / wsum};
}

void leaningText(const Traits& t, char* out, int n) {
    static const char* pos[] = {"SUN", "HUNTER", "PLUMP"};
    static const char* neg[] = {"GLOW", "GRAZER", "SLEEK"};
    float v[3] = {t.sun, t.hunt, t.plump};
    int   a = -1, b = -1;
    for (int i = 0; i < 3; i++) {
        if (fabsf(v[i]) < 0.15f) continue;
        if (a < 0 || fabsf(v[i]) > fabsf(v[a])) b = a, a = i;
        else if (b < 0 || fabsf(v[i]) > fabsf(v[b])) b = i;
    }
    if (a < 0) snprintf(out, n, "BALANCED");
    else if (b < 0) snprintf(out, n, "%s", v[a] > 0 ? pos[a] : neg[a]);
    else snprintf(out, n, "%s, %s", v[a] > 0 ? pos[a] : neg[a], v[b] > 0 ? pos[b] : neg[b]);
}

float fitness(uint32_t meals, uint32_t wins, float hpFrac) { return meals + 3.0f * wins + 5.0f * hpFrac; }

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

// zeroed first, padding included, so the CRC only ever sees defined bytes
static Save blank() {
    Save s;
    memset(&s, 0, sizeof s);
    s.magic   = 0xCE11;
    s.version = 2;
    s.body.stage = 1;
    s.hp01    = 1;
    s.energy  = 70;
    return s;
}

Save freshSave(uint32_t seed) {
    Save s = blank();
    for (int i = 0; i < DROP; i++) {
        s.drop[i]      = randomGenome(seed);
        s.dropAlive[i] = 1;
    }
    return s;
}

void seal(Save& s) { s.crc = crc32((const uint8_t*)&s, offsetof(Save, crc)); }

bool valid(const Save& s, size_t bytesRead) {
    return bytesRead == sizeof(Save) && s.magic == 0xCE11 && s.version == 2 &&
           s.crc == crc32((const uint8_t*)&s, offsetof(Save, crc));
}

bool migrateV1(const void* bytes, size_t n, Save& out) {
    SaveV1 v;
    if (n != sizeof v) return false;
    memcpy(&v, bytes, sizeof v);
    if (v.magic != 0xCE11 || v.version != 1 || v.crc != crc32((const uint8_t*)&v, offsetof(SaveV1, crc))) return false;
    out = blank();  // the new fields start empty
    memcpy(&out.evolveSecs, &v.evolveSecs, offsetof(SaveV1, crc) - offsetof(SaveV1, evolveSecs));
    return true;
}

Save upgradeOld(uint8_t, float, uint32_t kills, float energy, int64_t born) {
    Save s    = blank();  // levels and xp are gone: it starts evolving at stage 1, neutral
    s.havePet = 1;
    s.kills   = kills;
    s.energy  = energy;
    s.born    = born;
    s.pet.size = s.pet.speed = 1;
    s.pet.hue  = 120;
    return s;
}

LoadFrom planLoad(size_t pet2Bytes, bool pet2Valid, bool migratedV1, bool legacyKeys) {
    if (pet2Bytes && pet2Valid) return LoadFrom::Pet2;
    if (pet2Bytes && migratedV1) return LoadFrom::Migrated;
    if (pet2Bytes) return LoadFrom::Fresh;  // there was a pet here and it won't load: start over, never go back to the old one
    return legacyKeys ? LoadFrom::Legacy : LoadFrom::Fresh;
}

bool roundOneEmpty(const Save& s) {
    if (s.havePet) return false;
    for (int i = 0; i < DROP; i++)
        if (s.dropAlive[i]) return false;
    return true;
}

uint32_t animMs(uint32_t ms) { return ms & (ANIM_WRAP_MS - 1); }

}  // namespace evo
