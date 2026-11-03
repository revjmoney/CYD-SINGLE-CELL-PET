// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "quips.h"
#include <string.h>
#include "quips_data.h"

static uint32_t next(uint32_t& s) {
    if (!s) s = 0x2545F491u;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
}
static uint32_t gcd(uint32_t a, uint32_t b) {
    while (b) a %= b, a ^= b, b ^= a, a ^= b;
    return a;
}

int quipPool(int stage, bool havePet) {
    if (!havePet) return 0;
    return stage < 1 ? 1 : stage >= QUIP_POOLS ? QUIP_POOLS - 1 : stage;
}
int         quipCount(int pool) { return pool >= 0 && pool < QUIP_POOLS ? poolSizes[pool] : 0; }
const char* quipLine(int pool, int i) { return pools[pool][i]; }

const char* quipPick(int pool, uint32_t& order, uint16_t& pos, uint32_t& rng) {
    uint32_t n = quipCount(pool);
    if (!order || pos >= n) {  // a fresh shuffle: a random start and a step that visits every line once
        uint32_t step;
        do step = 1 + next(rng) % (n > 1 ? n - 1 : 1);
        while (n > 1 && gcd(step, n) != 1);
        order = (next(rng) % n) << 16 | step;
        pos   = 0;
    }
    uint32_t start = order >> 16, step = order & 0xFFFF;
    return pools[pool][(start + pos++ * step) % n];
}

uint32_t quipDelayMs(uint32_t& rng) { return 180000 + next(rng) % (2700000 - 180000 + 1); }

const char* quipEvent(QuipEvent e) { return events[e]; }
bool        quipEventOnce(uint32_t& done, QuipEvent e) {
    if (done & (1u << e)) return false;
    done |= 1u << e;
    return true;
}

int quipWrap(const char* s, char* line1, char* line2) {
    line1[0] = line2[0] = 0;
    int n = strlen(s);
    if (n <= 38) {
        strcpy(line1, s);
        return 1;
    }
    int cut = -1;  // the last space that leaves the first line at 38 or less
    for (int i = 0; i <= 38 && i < n; i++)
        if (s[i] == ' ') cut = i;
    if (cut < 0 || n - cut - 1 > 38) return 0;
    memcpy(line1, s, cut), line1[cut] = 0;
    strcpy(line2, s + cut + 1);
    return 2;
}
