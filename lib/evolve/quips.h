// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Silly achievements: a pop-up line every 3 to 45 minutes, from a big pool for
// the pet's stage (round one has its own), never repeating until the pool is
// used up; plus a few lines tied to real events, once per pet.
#pragma once
#include <stdint.h>

// a pet's book of lines: Cell Pet's, or Duck Pet's (which adds a villain pool, 6)
struct QuipBook {
    const char* const* const* pools;
    const int*                sizes;
    int                       nPools;
    const char* const*        events;
    int                       nEvents;
};
extern const QuipBook cellBook, duckBook;

enum QuipEvent : uint8_t {
    QE_CANNIBAL,  // the pet's first meal of another cell (or duck)
    QE_CROWNED,
    QE_STAGE2,
    QE_STAGE3,
    QE_STAGE4,
    QE_STAGE5,
    QE_FIRST_BOP,
    QE_FIRST_PAUSE,
    QE_MEALS100,
    QE_EVIL_WIN,  // duck: beat an evil duckling
    QE_FIRST_FROG,
    QE_FIRST_FISH,
    QE_COUNT
};

int         quipPool(int stage, bool havePet, bool villain);  // 0 round one, 1..5 the stages, 6 a villain's
int         quipCount(const QuipBook& b, int pool);
const char* quipLine(const QuipBook& b, int pool, int i);
// the next line from a pool: walks a shuffled order (start and step packed in
// `order`), and starts a fresh shuffle once pos has been through them all
const char* quipPick(const QuipBook& b, int pool, uint32_t& order, uint16_t& pos, uint32_t& rng);
uint32_t    quipDelayMs(uint32_t& rng);  // 3 to 45 minutes
const char* quipEvent(const QuipBook& b, QuipEvent e);  // nullptr if this book has no line for it
bool        quipEventOnce(uint32_t& done, QuipEvent e);  // true the first time only
int         quipWrap(const char* s, char* line1, char* line2);  // into lines of up to 38; returns how many lines (0 if it won't fit in 2)
