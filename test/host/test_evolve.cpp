// Host tests for the evolution rules: powershell -File tools\test.ps1
#include "check.h"
#include "evolve.h"
#include "quips.h"
#include "duckrules.h"
#include <cstddef>
#include <cstring>
#include <string>

using namespace evo;

static void testStages() {
    CHECK(stageFor(0) == 1);
    CHECK(stageFor(129599) == 1);
    CHECK(stageFor(129600) == 2);
    CHECK(stageFor(518400) == 5);
    CHECK(stageFor(4000000000u) == 5);
    CHECK_NEAR(stageProgress(0), 0, 1e-6);
    CHECK_NEAR(stageProgress(64800), 0.5, 1e-6);
    CHECK_NEAR(stageProgress(518400), 1, 1e-6);
}

static void testTickCarriesMilliseconds() {
    Clock c;
    CHECK(tick(c, 1000) == 0);  // the first call only starts the clock
    CHECK(tick(c, 3500) == 2);
    CHECK(tick(c, 6000) == 3);  // 2500 + the 500 carried over
    CHECK(c.evolveSecs == 5);
}

static void testTickSurvivesMillisWrap() {
    Clock c;
    tick(c, 0xFFFFF000u);
    CHECK(tick(c, 0x00000BB8u) == 7);  // 4096 + 3000 ms across the wrap
}

static void testPausedAddsNothing() {
    Clock c;
    tick(c, 0);
    c.paused = true;
    CHECK(tick(c, 10000) == 0);
    c.paused = false;
    CHECK(tick(c, 11000) == 1);  // no catch-up for the paused time
    CHECK(c.evolveSecs == 1);
}

static void testLightCountsDaytimeOnly() {
    Tally t{};
    addLight(t, 1.0f, 3, true);
    CHECK(t.lightN == 0);  // 3 AM with a set clock: nights don't count
    addLight(t, 1.0f, 3, false);
    CHECK(t.lightN == 1);  // clock unset: every hour counts
    addLight(t, 1.0f, 7, true);
    addLight(t, 1.0f, 21, true);
    CHECK(t.lightN == 3);
    addLight(t, 1.0f, 22, true);
    CHECK(t.lightN == 3);
}

static void testFightTally() {
    Tally t{};
    CHECK_NEAR(tallyTraits(t).hunt, -1, 1e-6);  // never fought: pure grazer
    for (int i = 0; i < 20; i++) addFight(t, true);
    CHECK_NEAR(tallyTraits(t).hunt, 1, 1e-6);
}

static void testFeedTally() {
    Tally t{};
    CHECK_NEAR(tallyTraits(t).plump, 0, 1e-6);
    for (int i = 0; i < 10; i++) addMeal(t, true);
    CHECK_NEAR(tallyTraits(t).plump, 1, 1e-6);
    CHECK_NEAR(tallyTraits(t).sun, 0, 1e-6);  // no light samples yet: neutral
}

static void testLockInWeighsLaterStagesMore() {
    Body b{};
    b.stage = 1;
    lockStage(b, Traits{1, 0, 0});
    CHECK(b.stage == 2);
    CHECK_NEAR(b.locked[0].sun, 1, 1e-6);
    CHECK_NEAR(shapeOf(b, Traits{-1, 0, 0}).sun, (1 * 1 + 2 * -1) / 3.0, 1e-5);
}

static void testLeaningText() {
    char s[40];
    leaningText(Traits{0.1f, 0.9f, -0.5f}, s, sizeof s);
    CHECK(std::string(s) == "HUNTER, SLEEK");
    leaningText(Traits{0, 0, 0}, s, sizeof s);
    CHECK(std::string(s) == "BALANCED");
    leaningText(Traits{-0.8f, 0.05f, 0.1f}, s, sizeof s);
    CHECK(std::string(s) == "GLOW");
}

static void testRandomGenome() {
    uint32_t a = 12345, b = 12345;
    Genome g1 = randomGenome(a), g2 = randomGenome(b);
    CHECK(g1.lean.sun == g2.lean.sun && g1.size == g2.size && g1.hue == g2.hue);
    bool ok = true;
    uint32_t r = 99;
    for (int i = 0; i < 1000; i++) {
        Genome g = randomGenome(r);
        for (float v : {g.lean.sun, g.lean.hunt, g.lean.plump}) ok &= v >= -1 && v <= 1;
        ok &= g.size >= 0.8f && g.size <= 1.2f && g.speed >= 0.8f && g.speed <= 1.2f;
    }
    CHECK(ok);
}

static void testFitness() {
    CHECK_NEAR(fitness(10, 2, 1.0f), 10 + 6 + 5, 1e-6);
    CHECK(WIN_FITNESS == 40);
}

static void testSaveRoundTrips() {
    Save s = freshSave(1);
    seal(s);
    unsigned char buf[sizeof(Save)];
    memcpy(buf, &s, sizeof s);
    Save back;
    memcpy(&back, buf, sizeof back);
    CHECK(valid(back, sizeof back));
    CHECK(back.havePet == 0);
    CHECK_NEAR(back.energy, 70, 1e-6);
    int alive = 0;
    for (int i = 0; i < 8; i++) alive += back.dropAlive[i];
    CHECK(alive == 8);
}

static void testDamagedSaveIsRejected() {
    Save s = freshSave(2);
    seal(s);
    Save flipped = s;
    ((unsigned char*)&flipped)[20] ^= 0x40;
    CHECK(!valid(flipped, sizeof flipped));
    CHECK(!valid(s, sizeof s - 1));  // cut short
    Save wrong = s;
    wrong.magic = 0x1234;
    seal(wrong);
    CHECK(!valid(wrong, sizeof wrong));
}

static void testUpgradeFromSkeletonPet() {
    Save s = upgradeOld(5, 12, 33, 60, 1730000000);
    CHECK(s.havePet == 1);
    CHECK(s.body.stage == 1);
    CHECK(s.kills == 33);
    CHECK(s.born == 1730000000);
    CHECK(s.evolveSecs == 0);
    CHECK_NEAR(s.energy, 60, 1e-6);
    CHECK(s.pet.lean.sun == 0 && s.pet.lean.hunt == 0 && s.pet.lean.plump == 0);
    seal(s);
    CHECK(valid(s, sizeof s));
}

// every line of a book fits two lines of 38 and carries no dashes
static void checkBookLines(const QuipBook& book, bool& ok, bool& dashes, int& total) {
    for (int pool = 0; pool < book.nPools; pool++)
        for (int i = 0; i < quipCount(book, pool); i++) {
            const char* q = quipLine(book, pool, i);
            char        a[40], b[40];
            int         n = quipWrap(q, a, b);
            total++;
            if (strlen(q) > 76 || n < 1 || n > 2 || strlen(a) > 38 || strlen(b) > 38) {
                ok = false;
                std::printf("  too long: %s\n", q);
            }
            if (strstr(q, " -- ") || strstr(q, "\xE2\x80\x94")) dashes = true;
        }
    for (int e = 0; e < QE_COUNT; e++) {
        const char* q = quipEvent(book, (QuipEvent)e);
        if (!q) continue;
        char a[40], b[40];
        int  n = quipWrap(q, a, b);
        if (n < 1 || n > 2) ok = false, std::printf("  event too long: %s\n", q);
        if (strstr(q, " -- ") || strstr(q, "\xE2\x80\x94")) dashes = true;
    }
}

static void testQuipLinesFitTwoLines() {
    bool ok = true, dashes = false;
    int  total = 0;
    checkBookLines(cellBook, ok, dashes, total);
    CHECK(ok);
    CHECK(!dashes);
}

static void testCellBookUnchanged() {
    CHECK(cellBook.nPools == 6);
    int want[6] = {20, 28, 24, 24, 24, 30};
    for (int p = 0; p < 6; p++) CHECK(quipCount(cellBook, p) == want[p]);
}

static void testDuckBookShape() {
    CHECK(duckBook.nPools == 7);
    for (int p = 0; p < duckBook.nPools; p++) CHECK(quipCount(duckBook, p) >= 20);
    bool ok = true, dashes = false;
    int  total = 0;
    checkBookLines(duckBook, ok, dashes, total);
    CHECK(ok);
    CHECK(!dashes);
    CHECK(total >= 150);
    for (QuipEvent e : {QE_EVIL_WIN, QE_FIRST_FROG, QE_FIRST_FISH, QE_CROWNED}) CHECK(quipEvent(duckBook, e) != nullptr);
    CHECK(quipEvent(cellBook, QE_FIRST_FROG) == nullptr);  // the cell has no frogs
}

static void testQuipNoRepeatsUntilPoolUsedUp() {
    for (const QuipBook* book : {&cellBook, &duckBook})
        for (int pool = 0; pool < book->nPools; pool++) {
            int         n = quipCount(*book, pool);
            uint32_t    order = 0, rng = 7 + pool;
            uint16_t    pos = 0;
            std::string seen[64];
            bool        dup = false;
            for (int i = 0; i < n; i++) {
                seen[i] = quipPick(*book, pool, order, pos, rng);
                for (int j = 0; j < i; j++) dup |= seen[j] == seen[i];
            }
            CHECK(!dup);
            quipPick(*book, pool, order, pos, rng);  // the next one starts a fresh shuffle
            CHECK(pos == 1);
        }
}

static void testQuipDelayRange() {
    uint32_t rng = 42, lo = ~0u, hi = 0;
    for (int i = 0; i < 10000; i++) {
        uint32_t d = quipDelayMs(rng);
        lo = d < lo ? d : lo, hi = d > hi ? d : hi;
    }
    CHECK(lo >= 180000);
    CHECK(hi <= 2700000);
    CHECK(hi - lo > 2000000);  // it really spreads out
}

static void testQuipEventFiresOnce() {
    uint32_t done = 0;
    CHECK(quipEventOnce(done, QE_CANNIBAL));
    CHECK(!quipEventOnce(done, QE_CANNIBAL));
    CHECK(quipEventOnce(done, QE_STAGE3));
}

static void testQuipPoolForStage() {
    CHECK(quipPool(1, false, false) == 0);  // round one
    CHECK(quipPool(1, true, false) == 1);
    CHECK(quipPool(5, true, false) == 5);
}

static void testVillainPool() {
    CHECK(quipPool(3, true, true) == 6);
    CHECK(quipPool(3, true, false) == 3);
    CHECK(quipPool(1, false, true) == 0);  // round one has no villain pool
}

// the save's CRC-32 (reflected, 0xEDB88320), worked out here independently
static uint32_t testCrc32(const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static void testSaveV1Migrates() {
    SaveV1 old;
    memset(&old, 0, sizeof old);
    old.magic = 0xCE11, old.version = 1, old.havePet = 1, old.kills = 7, old.evolveSecs = 1234;
    old.body.stage = 3;
    old.crc = testCrc32(&old, offsetof(SaveV1, crc));
    Save s;
    CHECK(migrateV1(&old, sizeof old, s));
    CHECK(s.version == 2 && s.havePet == 1 && s.kills == 7 && s.evolveSecs == 1234 && s.body.stage == 3);
    CHECK(s.quipDone == 0 && s.quipPos == 0);
    seal(s);
    CHECK(valid(s, sizeof s));
    CHECK(!migrateV1(&old, sizeof old - 1, s));
    old.kills = 8;  // damaged after its CRC was taken
    CHECK(!migrateV1(&old, sizeof old, s));
}

// review: a pet2 blob that exists but won't load must start a fresh drop,
// never bring the old demoscene pet back over it
static void testLoadPlanNeverRevivesLegacyOverPet2() {
    CHECK(planLoad(sizeof(Save), true, false, true) == LoadFrom::Pet2);
    CHECK(planLoad(sizeof(SaveV1), false, true, true) == LoadFrom::Migrated);
    CHECK(planLoad(sizeof(Save), false, false, true) == LoadFrom::Fresh);  // damaged pet2
    CHECK(planLoad(sizeof(SaveV1), false, false, true) == LoadFrom::Fresh);  // damaged v1
    CHECK(planLoad(17, false, false, true) == LoadFrom::Fresh);             // some other size
    CHECK(planLoad(0, false, false, true) == LoadFrom::Legacy);             // no pet2 yet: bring the old pet across once
    CHECK(planLoad(0, false, false, false) == LoadFrom::Fresh);
}

// review: a round one whose contenders are all gone (an old save) must not run forever
static void testEmptyRoundOneIsDetected() {
    Save s = freshSave(3);
    CHECK(!roundOneEmpty(s));
    for (int i = 0; i < DROP; i++) s.dropAlive[i] = 0;
    CHECK(roundOneEmpty(s));
    s.havePet = 1;
    CHECK(!roundOneEmpty(s));  // a pet exists: round one is over anyway
}

// review: animation time stays small so float phases don't go coarse over weeks of uptime
static void testAnimClockStaysSmall() {
    CHECK(animMs(0) == 0);
    CHECK(animMs(1000) == 1000);
    CHECK(animMs(0xFFFFFFFFu) < ANIM_WRAP_MS);
    CHECK(animMs(7u * 24 * 3600 * 1000) < ANIM_WRAP_MS);
    // at the largest value, a 30 ms frame step is still resolved finely in float seconds
    float a = animMs(ANIM_WRAP_MS - 31) * 0.001f, b = animMs(ANIM_WRAP_MS - 1) * 0.001f;
    CHECK_NEAR(b - a, 0.030, 0.0005);
}

static double rate(int n, bool (*f)(uint32_t&), uint32_t seed) {
    int hit = 0;
    for (int i = 0; i < n; i++) hit += f(seed);
    return hit / (double)n;
}

static void testEvilOdds() {
    uint32_t rng = 1234;
    int      evil = 0, n = 200000;
    bool     inRange = true;
    for (int i = 0; i < n; i++) {
        int k = duck::clutchEvil(rng);
        if (k >= 0) evil++;
        inRange &= k >= -1 && k <= 7;
    }
    CHECK_NEAR(evil / (double)n, 0.04, 0.003);
    CHECK(inRange);
    CHECK_NEAR(rate(n, duck::wildEvil, 99), 0.03, 0.003);
    CHECK_NEAR(rate(n, duck::normalBeatsEvil, 7), 0.12, 0.005);
}

static void testEvilFightNeverKillsYourDuck() {
    for (int i = 0; i < 8; i++) {
        bool upset = i & 1, evilIsPet = i & 2, normalIsPet = i & 4;
        if (evilIsPet && normalIsPet) continue;
        duck::EvilOutcome o = duck::evilFightOutcome(upset, evilIsPet, normalIsPet);
        CHECK(!(evilIsPet && o.evilDies));
        CHECK(!(normalIsPet && o.normalDies));
        CHECK(!(o.evilDies && o.normalDies));
    }
}

static void testUpsetKillsTheEvilDuckling() {
    duck::EvilOutcome o = duck::evilFightOutcome(true, false, false);
    CHECK(o.evilDies);
    CHECK(!o.normalDies);
}

static void testEvilPetEatsItsKill() {
    duck::EvilOutcome o = duck::evilFightOutcome(false, true, false);
    CHECK(o.normalDies);
    CHECK(o.petEnergy == duck::EVIL_MEAL);
    CHECK(duck::EVIL_MEAL > duck::preyEnergy(duck::FROG));
}

static void testBeatenPetLosesEnergy() {
    CHECK(duck::evilFightOutcome(false, false, true).petEnergy < 0);
    CHECK(duck::evilFightOutcome(true, true, false).petEnergy == 0);
}

static void testPrey() {
    using namespace duck;
    CHECK(preyEnergy(FROG) > preyEnergy(FISH));
    CHECK(preyEnergy(FISH) > preyEnergy(BUG));
    CHECK(preyEnergy(BUG) > PEAS);
    CHECK(PEAS > WEED);
    uint32_t rng = 5;
    bool     ok  = true;
    double   dark = 0, bright = 0;
    for (int i = 0; i < 2000; i++) {
        uint32_t d0 = preyDelayMs(BUG, 0, rng), d1 = preyDelayMs(BUG, 1, rng);
        ok &= d0 >= 30000 / 0.6 * 0.999 && d0 <= 90000 / 0.6 * 1.001 && d1 >= 30000 / 1.4 * 0.999 && d1 <= 90000 / 1.4 * 1.001;
        dark += d0, bright += d1;
        uint32_t f = preyDelayMs(FROG, 0.5f, rng), g = preyDelayMs(FISH, 0.5f, rng);
        ok &= f >= 180000 / 1.0 * 0.999 && f <= 360000 * 1.001 && g >= 120000 * 0.999 && g <= 300000 * 1.001;
    }
    CHECK(ok);
    CHECK(bright < dark);  // more prey in a bright room
}

static void testDuckFlags() {
    using namespace duck;
    DuckFlags a = unpackFlags(packFlags({true, 0x80}));
    CHECK(a.petEvil && a.dropEvil == 0x80);
    DuckFlags b = unpackFlags(packFlags({false, 0}));
    CHECK(!b.petEvil && b.dropEvil == 0);
}

int main() {
    testStages();
    testTickCarriesMilliseconds();
    testTickSurvivesMillisWrap();
    testPausedAddsNothing();
    testLightCountsDaytimeOnly();
    testFightTally();
    testFeedTally();
    testLockInWeighsLaterStagesMore();
    testLeaningText();
    testRandomGenome();
    testFitness();
    testSaveRoundTrips();
    testDamagedSaveIsRejected();
    testUpgradeFromSkeletonPet();
    testQuipLinesFitTwoLines();
    testCellBookUnchanged();
    testDuckBookShape();
    testVillainPool();
    testEvilOdds();
    testPrey();
    testEvilFightNeverKillsYourDuck();
    testUpsetKillsTheEvilDuckling();
    testEvilPetEatsItsKill();
    testBeatenPetLosesEnergy();
    testDuckFlags();
    testQuipNoRepeatsUntilPoolUsedUp();
    testQuipDelayRange();
    testQuipEventFiresOnce();
    testQuipPoolForStage();
    testSaveV1Migrates();
    testLoadPlanNeverRevivesLegacyOverPet2();
    testEmptyRoundOneIsDetected();
    testAnimClockStaysSmall();
    return checkReport();
}
