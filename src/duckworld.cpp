// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Duck Pet's pond, in screen pixels. Round one: a clutch of eight glossy
// ducklings and no pet; the first to prove itself (meals, squabbles won,
// health) is crowned and becomes yours. From then on your duck grows on the
// stage clock while its care (room light, squabbles, feeding) is tallied and
// locked into its body, just like Cell Pet.
// Ducklings squabble (pecks and shoves, nobody gets hurt for good). Now and
// then one hatches evil: it hunts the others and eats them, unless a brave
// duckling gets the 12% upset. Your duck never dies; if an evil one gets it,
// it flees hurt and recovers. Bugs, frogs and goldfish turn up to be caught,
// and a turtle paddles across now and then.
#include "duckworld.h"
#include <Arduino.h>
#include <Preferences.h>
#include "duckrules.h"
#include "led.h"
#include "net.h"
#include "pet.h"
#include "quips.h"
#include "save.h"
#include "snapshot.h"

namespace cp {

namespace {

constexpr int   W = SW, H = SH;
constexpr float CX = W / 2.0f, CY = H / 2.0f + 10;
constexpr float LEFT = 14, RIGHT = W - 14, TOP = 40, BOTTOM = H - 10;  // where ducks can paddle
constexpr int   NDUCK = 12, NFOOD = 32, NPREY = 8, NSPLAT = 64;
const float     pads[3][2] = {{40, 200}, {285, 60}, {250, 210}};  // lily pads, as drawn (frogs hop in and out here)

float frand(float a, float b) { return a + (b - a) * (esp_random() & 0xFFFF) / 65535.0f; }

struct Duck {
    float       x, y, vx, vy, size, hp, face, ph, calm, dabble, flap, alpha, eaten, startle, quack, preen, nextQuack, nextPreen;
    float       spare;  // an evil one that just beat your duck leaves it alone this long
    uint8_t     alive, role, evil, leaving;
    int8_t      drop, eater;
    evo::Traits lean;
    float       speed;
};
struct Food {
    float   x, y, vx, vy;
    uint8_t type, alive, fed;  // type 0 duckweed, 1 peas and oats
};
struct Critter {  // prey: a bug, a frog or a goldfish
    float   x, y, vx, vy, phase, depth, life, kick;
    uint8_t type, alive;
};
struct Splat {
    float   x, y, vx, vy, life;
    uint8_t red;
};

struct Ducks {
    Duck            d[NDUCK];
    Food            food[NFOOD];
    Critter         prey[NPREY];
    Splat           splat[NSPLAT];
    evo::Save       s;
    evo::Clock      clock;
    duck::DuckFlags flags{};
    float      light = 0.6f, joy = 0, blinkT = 0, crownT = 0, shimmer = 0, glow = 0, turtleX = -200, turtleY = 150, turtleDir = 1;
    float      fitBoost = 1;
    bool       holdRound = false, override = false, night = false, forceNight = false, frozen = false;
    evo::Traits forced{};
    uint32_t   last = 0, lightMs = 0, weedMs = 0, saveMs = 0, tallyMs = 0, castMs = 0, turtleMs = 0, rng = 0, virt = 0, virtStart = 0;
    uint32_t   preyAt[3] = {}, quipAt = 0;
    time_t     epochAtFreeze = 0;
    char       hud[48], bigTime[8], info[40], leaning[40], banner[2][40];
    float      bannerT = 0, bannerAge = 0;
    QuipEvent  queued[4];
    int        nQueued = 0;

    uint32_t nowMs() const { return frozen ? virt : millis(); }
    bool     timeOk() const { return time(nullptr) > 1700000000; }
    bool     petEvil() const { return s.havePet && flags.petEvil; }
    evo::Traits shape() const {
        if (override) return forced;
        return evo::shapeOf(s.body, s.body.stage >= 5 ? s.body.locked[4] : evo::tallyTraits(s.tally));
    }
    float petSize() const {
        static const float grow[5] = {1.0f, 1.3f, 1.6f, 2.0f, 2.6f};
        int st = s.body.stage < 1 ? 1 : s.body.stage > 5 ? 5 : s.body.stage;
        return 13 * (0.85f + 0.3f * (shape().plump + 1) / 2) * grow[st - 1];
    }
    float maxHp(const Duck& k) const { return k.size * 10; }

    // ---------------------------------------------------------------- setting up
    void loadFlags() {
        Preferences p;
        p.begin("duck1", true);
        bool has = p.isKey("x");
        flags = duck::unpackFlags(p.getULong("x", 0));
        p.end();
        if (!has && !s.havePet) newClutchFlags();
    }
    void newClutchFlags() {
        int e = duck::clutchEvil(rng);  // 4% of clutches have one: about 3% of pets end up evil
        flags = {false, (uint8_t)(e >= 0 ? 1 << e : 0)};
    }
    void begin() {
        rng = esp_random();
        s   = petLoad("duck1");
        loadFlags();
        if (evo::roundOneEmpty(s)) s = evo::freshSave(esp_random()), newClutchFlags();
        clock.evolveSecs = s.evolveSecs, clock.paused = s.paused;
        memset(d, 0, sizeof d), memset(food, 0, sizeof food), memset(prey, 0, sizeof prey), memset(splat, 0, sizeof splat);
        for (int i = 0; i < 14; i++) addFood(frand(LEFT, RIGHT), frand(TOP, BOTTOM), 0, false);
        if (s.havePet) {
            newDuck(0, DUCK_PET);
            d[0].x = CX, d[0].y = CY, d[0].alpha = 1;
            d[0].hp = max(1.0f, s.hp01 * maxHp(d[0]));
            for (int i = 1; i <= 6; i++) newDuck(i, DUCKLING), d[i].alpha = 1, d[i].x = frand(LEFT + 20, RIGHT - 20), d[i].y = frand(TOP + 20, BOTTOM - 20);
        } else
            startRoundOne();
        pinMode(34, INPUT);  // the light sensor on the front of the board
        analogSetPinAttenuation(34, ADC_0db);  // its most sensitive range: this sensor only swings a few hundred mV
        last = saveMs = tallyMs = castMs = turtleMs = millis();
        for (int p = 0; p < 3; p++) preyAt[p] = last + duck::preyDelayMs((duck::Prey)p, light, rng) / 3;
        quipAt = last + quipDelayMs(rng);
    }
    void startRoundOne() {
        memset(d, 0, sizeof d);
        for (int i = 0; i < evo::DROP; i++)
            if (s.dropAlive[i]) {
                newDuck(i, DUCKLING, i);
                d[i].x = frand(LEFT + 25, RIGHT - 25), d[i].y = frand(TOP + 20, BOTTOM - 15), d[i].alpha = 1;
            }
    }
    void newDuck(int i, uint8_t role, int drop = -1) {
        Duck& k = d[i];
        k       = {};
        k.alive = 1, k.role = role, k.drop = drop, k.eater = -1, k.alpha = 0.01f, k.face = 1, k.ph = frand(0, 6.283f);
        k.nextQuack = frand(8, 30), k.nextPreen = frand(15, 40), k.speed = 1;
        // swim in from an edge
        if (frand(0, 1) < 0.5f) k.x = frand(0, 1) < 0.5f ? LEFT : RIGHT, k.y = frand(TOP, BOTTOM);
        else k.x = frand(LEFT, RIGHT), k.y = frand(0, 1) < 0.5f ? TOP : BOTTOM;
        if (role == DUCK_PET) {
            k.size = petSize(), k.lean = s.pet.lean, k.speed = s.pet.speed, k.evil = flags.petEvil;
        } else if (drop >= 0) {
            const evo::Genome& g = s.drop[drop];
            k.size = 13 * g.size, k.lean = g.lean, k.speed = g.speed, k.evil = (flags.dropEvil >> drop) & 1;
        } else {
            k.size = 13 * frand(0.8f, 1.1f), k.speed = frand(0.85f, 1.15f), k.evil = duck::wildEvil(rng);
            k.lean = {frand(-1, 1), frand(-1, 1), frand(-1, 1)};
        }
        k.hp = maxHp(k);
    }
    void addFood(float x, float y, uint8_t type, bool fed) {
        for (auto& f : food)
            if (!f.alive) {
                f = {x, y, frand(-2, 2), frand(-2, 2), type, 1, fed};
                return;
            }
    }

    // ---------------------------------------------------------------- life
    void steer(Duck& k, float tx, float ty, float accel, float dt) {
        float dx = tx - k.x, dy = ty - k.y, dd = sqrtf(dx * dx + dy * dy) + 1e-3f;
        k.vx += dx / dd * accel * dt, k.vy += dy / dd * accel * dt * 0.8f;  // a bit flatter up and down: the angled view
    }
    bool sleeping(const Duck& k) const { return night && k.startle <= 0 && !k.evil; }
    void think(int i, float dt, uint32_t t) {
        Duck& k     = d[i];
        bool  pet   = k.role != DUCKLING;
        float speed = 34 * k.speed * (k.hp < maxHp(k) * 0.3f ? 0.6f : 1) * (sleeping(k) ? 0.15f : 1);
        if (k.leaving) {  // paddling off: to the nearest edge, fading out
            float ex = k.x < CX ? -40 : W + 40;
            steer(k, ex, k.y, speed * 2, dt);
            return;
        }
        // the nearest food, prey, evil one, and (for an evil duck) victim
        int   ff = -1, pp = -1, ev = -1, vic = -1;
        float df = 1e9, dp = 1e9, de = 1e9, dv = 1e9;
        for (int j = 0; j < NFOOD; j++)
            if (food[j].alive) {
                float q = (food[j].x - k.x) * (food[j].x - k.x) + (food[j].y - k.y) * (food[j].y - k.y);
                if (q < df) df = q, ff = j;
            }
        for (int j = 0; j < NPREY; j++)
            if (prey[j].alive && (prey[j].type != duck::FISH || prey[j].depth < 0.5f)) {
                float q = (prey[j].x - k.x) * (prey[j].x - k.x) + (prey[j].y - k.y) * (prey[j].y - k.y);
                if (q < dp) dp = q, pp = j;
            }
        for (int j = 0; j < NDUCK; j++) {
            const Duck& o = d[j];
            if (j == i || !o.alive || o.eaten || o.leaving) continue;
            float q = (o.x - k.x) * (o.x - k.x) + (o.y - k.y) * (o.y - k.y);
            if (o.evil && !k.evil && q < de) de = q, ev = j;
            if (k.evil && !o.evil && q < dv && !(o.role != DUCKLING && k.spare > 0)) dv = q, vic = j;
        }
        // your duck eats when it's hungry; the others eat in spells, so food and prey are around to see
        bool hungry = pet ? s.energy < 85 : sinf(evo::animMs(t) * 0.00025f + k.ph * 3) > 0.35f || k.hp < maxHp(k) * 0.5f;
        if (k.calm > 2 && ev >= 0) steer(k, 2 * k.x - d[ev].x, 2 * k.y - d[ev].y, speed * 3, dt);  // just lost: get away
        else if (k.startle > 0) steer(k, k.x + k.vx * 10, k.y + k.vy * 10, speed * 3, dt);       // startled: keep going
        else if (k.evil && vic >= 0 && (!pet || s.energy < 90)) steer(k, d[vic].x, d[vic].y, speed * 2.6f, dt);  // hunting ducklings
        else if (ev >= 0 && de < 70 * 70) steer(k, 2 * k.x - d[ev].x, 2 * k.y - d[ev].y, speed * 2.4f, dt);  // keep away from evil
        else if (fabsf(turtleX - k.x) < 40 && fabsf(turtleY - k.y) < 26) steer(k, k.x, 2 * k.y - turtleY, speed * 2, dt);  // and the turtle
        else if (pp >= 0 && dp < 90 * 90 && hungry) steer(k, prey[pp].x, prey[pp].y, speed * (2.2f + 0.4f * k.lean.hunt), dt);  // a meal that moves
        else if (ff >= 0 && hungry && !sleeping(k)) steer(k, food[ff].x, food[ff].y, speed * (1.6f + 0.25f * (k.lean.plump + 1)), dt);
        else {
            float w  = k.ph + evo::animMs(t) * 0.0003f;  // paddling about
            float sx = CX + cosf(w) * 105, sy = CY + sinf(w * 1.3f) * 60;
            if (pet || k.drop >= 0) {  // sun-lovers drift to the middle in a lit room
                float pull = k.lean.sun * (light - 0.5f);
                sx += (CX - sx) * pull, sy += (CY - sy) * pull;
            }
            steer(k, sx, sy, speed, dt);
        }
        // eating: food and prey within reach of the bill
        float reach = k.size + 4;
        if (ff >= 0 && df < reach * reach && !sleeping(k)) {
            Food& f = food[ff];
            f.alive = 0, k.dabble = 0.6f;
            meal(k, f.type ? duck::PEAS : duck::WEED, f.fed);
        }
        if (pp >= 0 && dp < reach * reach) {
            Critter& c = prey[pp];
            bool     fast = c.type == duck::BUG && frand(0, 1) < 0.5f;  // bugs dodge half the time
            if (!fast) {
                c.alive = 0, k.dabble = 0.6f;
                meal(k, duck::preyEnergy((duck::Prey)c.type), false);
                if (pet && c.type == duck::FROG) event(QE_FIRST_FROG);
                if (pet && c.type == duck::FISH) event(QE_FIRST_FISH);
            }
        }
    }
    void meal(Duck& k, float energy, bool fed) {
        if (k.role != DUCKLING) {
            s.energy = min(100.0f, s.energy + energy), k.hp = min(maxHp(k), k.hp + energy * 1.5f);
            ledFlash(false, true, false, 80);
            if (!clock.paused) evo::addMeal(s.tally, fed);
        } else {
            k.hp = min(maxHp(k), k.hp + energy);
            if (k.drop >= 0 && !clock.paused) s.dropMeals[k.drop]++;
        }
    }

    // ---------------------------------------------------------------- squabbles and evil fights
    void spray(float x, float y, bool red, int n) {
        bool r = red && splatterRed;
        for (int m = 0; m < n; m++)
            for (auto& p : splat)
                if (p.life <= 0) {
                    float a = frand(0, 6.283f), v = frand(20, 70);
                    p = {x, y, cosf(a) * v, sinf(a) * v * 0.6f - 20, frand(0.9f, 1.8f), (uint8_t)r};
                    break;
                }
    }
    void fights(float dt) {
        float strength = s.havePet ? 0.6f : 0.35f;
        for (int i = 0; i < NDUCK; i++)
            for (int j = i + 1; j < NDUCK; j++) {
                Duck &a = d[i], &b = d[j];
                if (!a.alive || !b.alive || a.eaten || b.eaten || a.leaving || b.leaving || a.alpha < 0.8f || b.alpha < 0.8f) continue;
                float dx = b.x - a.x, dy = b.y - a.y, dd = sqrtf(dx * dx + dy * dy) + 1e-3f, o = (a.size + b.size) * 0.85f - dd;
                if (o <= 0) continue;
                a.vx -= dx / dd * o * 6 * dt, a.vy -= dy / dd * o * 6 * dt;  // bump apart
                b.vx += dx / dd * o * 6 * dt, b.vy += dy / dd * o * 6 * dt;
                a.flap = b.flap = 0.4f;
                if (a.calm > 0 || b.calm > 0) continue;
                if (a.evil != b.evil) {
                    const Duck& ev = a.evil ? a : b;
                    const Duck& no = a.evil ? b : a;
                    if (no.role != DUCKLING && ev.spare > 0) continue;  // it already got your duck: it lets it be
                    evilFight(a.evil ? i : j, a.evil ? j : i);
                    continue;
                }
                if (a.evil) continue;  // two evil ones leave each other alone
                // a squabble: pecks both ways, the bigger one pecks harder
                float pa = a.size * (a.role ? 1.2f + 0.3f * max(0.0f, a.lean.hunt) : 1), pb = b.size * (b.role ? 1.2f + 0.3f * max(0.0f, b.lean.hunt) : 1);
                a.hp -= pb * 3 * dt * strength, b.hp -= pa * 3 * dt * strength;
                if (a.hp <= 0) squabbleLost(i, j);
                else if (b.hp <= 0) squabbleLost(j, i);
            }
    }
    void squabbleLost(int loser, int winner) {
        Duck &l = d[loser], &w = d[winner];
        l.hp = maxHp(l) * 0.3f, l.calm = 4, w.calm = 2, l.quack = 0.8f;
        l.vx = (l.x - w.x) * 4, l.vy = (l.y - w.y) * 4;
        if (!clock.paused) {
            if (w.role != DUCKLING) evo::addFight(s.tally, true);
            if (l.role != DUCKLING) evo::addFight(s.tally, false);
            if (w.drop >= 0 && !s.havePet) s.dropWins[w.drop]++;
        }
    }
    // decided once, on contact: the normal duck wins 12%
    void evilFight(int evil, int normal) {
        Duck &e = d[evil], &n = d[normal];
        bool  upset = duck::normalBeatsEvil(rng);
        bool  petIn = e.role != DUCKLING || n.role != DUCKLING;
        Serial.printf("duck: evil fight, %s%s\n", upset ? "upset" : "evil wins", petIn ? " (your duck)" : "");
        if (petIn) ledFlash(true, false, false, 250);
        duck::EvilOutcome o = duck::evilFightOutcome(upset, e.role != DUCKLING, n.role != DUCKLING);
        s.energy = max(0.0f, min(100.0f, s.energy + o.petEnergy));
        if (o.evilDies && e.drop >= 0) s.dropAlive[e.drop] = 0;  // gone for good, even after a power cut
        if (o.normalDies && n.drop >= 0) s.dropAlive[n.drop] = 0;
        if (upset) {  // the brave one wins: the evil one bursts into dark feathers
            spray(e.x, e.y - e.size * 0.6f, false, 14);
            if (e.role != DUCKLING) {  // your evil duck lost: it flees hurt, it never dies
                e.hp = 1, e.calm = 4, e.vx = (e.x - n.x) * 4, e.vy = (e.y - n.y) * 4;
                if (!clock.paused) evo::addFight(s.tally, false);
                return;
            }
            e.eaten = 0.001f, e.eater = normal;
            if (n.role != DUCKLING) {
                event(QE_EVIL_WIN);
                if (!clock.paused) evo::addFight(s.tally, true);
            }
            if (n.drop >= 0 && !s.havePet) s.dropWins[n.drop]++;
            return;
        }
        // the evil one wins
        if (n.role != DUCKLING) {  // your duck never dies: it flees hurt
            n.hp = 1, n.calm = 4, n.vx = (n.x - e.x) * 4, n.vy = (n.y - e.y) * 4, n.quack = 0.8f;
            e.spare = 30;  // and the evil one goes after someone else for a while
            spray(n.x, n.y - n.size * 0.6f, false, 5);
            if (!clock.paused) evo::addFight(s.tally, false);
            return;
        }
        spray(n.x, n.y - n.size * 0.6f, true, 22);  // the splatter (feathers only, if the setting says so)
        n.eaten = 0.001f, n.eater = evil;
        e.calm = 1.5f;
        if (e.role != DUCKLING) {
            s.kills++, joy = 1.5f, e.hp = min(maxHp(e), e.hp + duck::EVIL_MEAL * 1.5f);
            event(QE_CANNIBAL);
            if (!clock.paused) evo::addFight(s.tally, true);
        } else if (e.drop >= 0 && !s.havePet)
            s.dropWins[e.drop]++;
    }

    // ---------------------------------------------------------------- round one
    void judgeRoundOne() {
        int   alive = 0, lastOne = -1, best = -1;
        float bestFit = 0;
        for (int i = 0; i < NDUCK; i++) {
            const Duck& k = d[i];
            if (!k.alive || k.eaten || k.drop < 0) continue;
            alive++, lastOne = i;
            float f = evo::fitness(s.dropMeals[k.drop] / 64, s.dropWins[k.drop] / 48, k.hp / maxHp(k)) * fitBoost;
            s.dropFit[k.drop] = f;
            if (f > bestFit) bestFit = f, best = i;
        }
        if (holdRound) return;
        if (bestFit >= evo::WIN_FITNESS) crown(best);
        else if (alive == 1) crown(lastOne);  // the last one paddling wins by default
    }
    void crown(int i) {
        Duck& k   = d[i];
        s.pet     = s.drop[k.drop];
        s.havePet = 1, s.energy = 80;
        flags.petEvil = k.evil;
        for (int n = 0; n < 10; n++) evo::addLight(s.tally, (k.lean.sun + 1) / 2, 12, false);
        int wins = (k.lean.hunt + 1) * 5, fed = (k.lean.plump + 1) * 5;
        for (int n = 0; n < 10; n++) evo::addFight(s.tally, n < wins);
        for (int n = 0; n < 10; n++) evo::addMeal(s.tally, n < fed);
        Duck winner = k;
        winner.role = DUCK_PET, winner.drop = -1, winner.lean = s.pet.lean, winner.speed = s.pet.speed;
        winner.size = petSize(), winner.hp = maxHp(winner);
        for (int j = 0; j < NDUCK; j++)
            if (j != i && d[j].alive) d[j].drop = -1;  // everyone else is just a duckling now
        if (i != 0) d[i] = d[0];
        d[0] = winner;
        for (int j = 0; j < NDUCK; j++)
            if (d[j].eater == i) d[j].eater = 0;
            else if (d[j].eater == 0 && i != 0) d[j].eater = i;
        crownT = 4, joy = 2, fitBoost = 1;
        ledFlash(true, true, true, 600);
        event(QE_CROWNED);
        store();
    }

    // ---------------------------------------------------------------- saving
    void store() {
        s.evolveSecs = clock.evolveSecs, s.paused = clock.paused;
        if (s.havePet) s.hp01 = d[0].hp / maxHp(d[0]);
        petStore(s, "duck1");
        Preferences p;
        p.begin("duck1");
        p.putULong("x", duck::packFlags(flags));
        p.end();
    }

    // ---------------------------------------------------------------- achievements
    void event(QuipEvent e) {
        if (!quipEventOnce(s.quipDone, e)) return;
        if (nQueued < 4) queued[nQueued++] = e;
    }
    void show(const char* line) {
        if (!line || !quipWrap(line, banner[0], banner[1])) return;
        bannerT = 6, bannerAge = 0;
        ledFlash(true, true, true, 150);
    }
    void quips(uint32_t now, float dt) {
        bannerAge += dt;
        if (bannerT > 0) {
            bannerT -= dt;
            if (bannerT <= 0) banner[0][0] = banner[1][0] = 0;
            return;
        }
        if (crownT > 0) return;
        if (nQueued) {
            show(quipEvent(duckBook, queued[0]));
            memmove(queued, queued + 1, --nQueued * sizeof queued[0]);
            return;
        }
        if ((int32_t)(now - quipAt) < 0) return;
        int pool = quipPool(s.body.stage, s.havePet, petEvil());
        if (pool != s.quipPool) s.quipPool = pool, s.quipOrder = 0, s.quipPos = 0;
        show(quipPick(duckBook, pool, s.quipOrder, s.quipPos, rng));
        quipAt = now + quipDelayMs(rng);
    }

    // ---------------------------------------------------------------- the pond around them
    void critters(uint32_t now, float dt) {
        for (int p = 0; p < 3; p++)
            if ((int32_t)(now - preyAt[p]) >= 0) {
                preyAt[p] = now + duck::preyDelayMs((duck::Prey)p, light, rng);
                for (auto& c : prey)
                    if (!c.alive) {
                        c = {};
                        c.alive = 1, c.type = p, c.phase = frand(0, 6.283f);
                        if (p == duck::FROG) {  // hops in from a lily pad
                            const float* pad = pads[esp_random() % 3];
                            c.x = pad[0], c.y = pad[1], c.life = 20;
                        } else
                            c.x = frand(LEFT + 20, RIGHT - 20), c.y = frand(TOP + 10, BOTTOM - 10), c.life = p == duck::BUG ? 40 : 60;
                        break;
                    }
            }
        for (auto& c : prey) {
            if (!c.alive) continue;
            c.life -= dt, c.phase += dt;
            if (c.type == duck::BUG) {  // skitters
                c.vx += frand(-160, 160) * dt, c.vy += frand(-120, 120) * dt;
                c.vx *= 1 - 3 * dt, c.vy *= 1 - 3 * dt;
            } else if (c.type == duck::FROG) {  // swims in kicks, then hops back out
                c.kick -= dt;
                if (c.kick <= 0) {
                    c.kick = frand(0.6f, 1.2f);
                    float a = frand(0, 6.283f);
                    c.vx = cosf(a) * 45, c.vy = sinf(a) * 30;
                    if (c.life < 3) {  // time to go: back to the nearest pad
                        float bx = pads[0][0], by = pads[0][1];
                        for (auto& p : pads)
                            if ((p[0] - c.x) * (p[0] - c.x) + (p[1] - c.y) * (p[1] - c.y) < (bx - c.x) * (bx - c.x) + (by - c.y) * (by - c.y)) bx = p[0], by = p[1];
                        c.vx = (bx - c.x) * 1.5f, c.vy = (by - c.y) * 1.5f;
                    }
                }
                c.vx *= 1 - 2.5f * dt, c.vy *= 1 - 2.5f * dt;
            } else {  // a goldfish: drifts, rising and sinking
                c.depth = 0.5f + 0.5f * sinf(c.phase * 0.7f);
                c.vx += frand(-20, 20) * dt, c.vy += frand(-15, 15) * dt;
                c.vx *= 1 - dt, c.vy *= 1 - dt;
            }
            c.x += c.vx * dt, c.y += c.vy * dt;
            if (c.x < LEFT || c.x > RIGHT) c.vx = -c.vx, c.x = constrain(c.x, LEFT, RIGHT);
            if (c.y < TOP || c.y > BOTTOM) c.vy = -c.vy, c.y = constrain(c.y, TOP, BOTTOM);
            if (c.life <= 0) c.alive = 0;
        }
        // duckweed grows with the light
        if (now - weedMs > 1200 / (0.3f + light)) {
            weedMs = now;
            int n = 0;
            for (auto& f : food) n += f.alive;
            if (n < 24) addFood(frand(LEFT, RIGHT), frand(TOP, BOTTOM), 0, false);
        }
        for (auto& f : food)
            if (f.alive) {
                f.x += f.vx * dt, f.y += f.vy * dt;
                if (f.x < LEFT || f.x > RIGHT) f.vx = -f.vx;
                if (f.y < TOP || f.y > BOTTOM) f.vy = -f.vy;
            }
        // the turtle: a slow paddle across now and then
        if (turtleX < -60 || turtleX > W + 60) {
            if (now - turtleMs > 90000 && frand(0, 1) < dt * 0.01f) {
                turtleMs = now, turtleDir = frand(0, 1) < 0.5f ? 1 : -1;
                turtleX = turtleDir > 0 ? -50 : W + 50, turtleY = frand(TOP + 30, BOTTOM - 20);
            }
        } else
            turtleX += turtleDir * 10 * dt, turtleY += sinf(now * 0.0004f) * 3 * dt;
        for (auto& p : splat)
            if (p.life > 0) p.x += p.vx * dt, p.y += p.vy * dt, p.vy += 80 * dt, p.vx *= 1 - dt, p.life -= dt;
    }
    // ducklings come and go after round one, so the pond keeps changing
    void cast(uint32_t now) {
        if (!s.havePet || now - castMs < 3000) return;
        castMs = now;
        int n = 0;
        for (int i = 1; i < NDUCK; i++) n += d[i].alive && !d[i].leaving;
        if (n > 5 && frand(0, 1) < 0.04f) {  // someone paddles off (about once a minute)
            int pick = 1 + esp_random() % (NDUCK - 1);
            if (d[pick].alive && !d[pick].eaten) d[pick].leaving = 1;
        }
        if (n < 5 || (n < 7 && frand(0, 1) < 0.05f))
            for (int i = 1; i < NDUCK; i++)
                if (!d[i].alive) {
                    newDuck(i, DUCKLING);
                    break;
                }
    }

    // ---------------------------------------------------------------- one step
    void update(uint32_t t) {
        uint32_t now = nowMs();
        float    dt  = min(0.05f, (now - last) / 1000.0f);
        last         = now;
        evo::tick(clock, now);
        if (now - lightMs > 250) {
            lightMs = now;
            float l = evo::roomLight(analogRead(34));
            light += (0.25f + 0.75f * l - light) * 0.1f;
        }
        time_t    tt = frozen ? epochAtFreeze + (time_t)((virt - virtStart) / 1000) : time(nullptr);
        struct tm lt;
        localtime_r(&tt, &lt);
        if (!s.born && timeOk()) s.born = tt, store();
        night = forceNight || (timeOk() && light < 0.3f && (lt.tm_hour >= 22 || lt.tm_hour < 7));
        glow += ((night ? 1.0f : 0.0f) - glow) * min(1.0f, dt * 0.5f);
        if (now - tallyMs > 10000) {
            tallyMs = now;
            if (s.havePet && !clock.paused) evo::addLight(s.tally, light, lt.tm_hour, timeOk());
        }
        critters(now, dt);
        cast(now);
        for (int i = 0; i < NDUCK; i++) {
            Duck& k = d[i];
            if (!k.alive) continue;
            if (k.eaten) {  // being eaten: shrinks into the winner
                Duck& w = d[k.eater];
                k.eaten += dt;
                k.x += (w.x - k.x) * 4 * dt, k.y += (w.y - k.y) * 4 * dt, k.size *= 1 - 1.6f * dt;
                if (k.eaten > 0.7f || k.size < 2) k.alive = 0;
                continue;
            }
            think(i, dt, t);
            k.vx *= 1 - 2.0f * dt, k.vy *= 1 - 2.0f * dt;
            if (!sleeping(k)) k.vx += frand(-8, 8) * dt, k.vy += frand(-6, 6) * dt;
            k.x += k.vx * dt, k.y += k.vy * dt;
            if (k.leaving) {
                k.alpha -= dt * 0.6f;
                if (k.alpha <= 0) k.alive = 0;
                continue;
            }
            k.alpha = min(1.0f, k.alpha + dt * 0.8f);
            // soft edges, then hard ones
            float m = k.size + 12;
            if (k.x < LEFT + m) k.vx += (LEFT + m - k.x) * 3 * dt;
            if (k.x > RIGHT - m) k.vx -= (k.x - (RIGHT - m)) * 3 * dt;
            if (k.y < TOP + m * 0.5f) k.vy += (TOP + m * 0.5f - k.y) * 3 * dt;
            if (k.y > BOTTOM - m * 0.5f) k.vy -= (k.y - (BOTTOM - m * 0.5f)) * 3 * dt;
            k.x = constrain(k.x, LEFT, RIGHT), k.y = constrain(k.y, TOP, BOTTOM);
            if (fabsf(k.vx) > 3) k.face += ((k.vx > 0 ? 1.0f : -1.0f) - k.face) * min(1.0f, 6 * dt);
            k.spare = max(0.0f, k.spare - dt), k.calm = max(0.0f, k.calm - dt), k.dabble = max(0.0f, k.dabble - dt), k.flap = max(0.0f, k.flap - dt);
            k.startle = max(0.0f, k.startle - dt), k.quack = max(0.0f, k.quack - dt), k.preen = max(0.0f, k.preen - dt);
            if (!sleeping(k)) {  // a quack and a preen now and then
                if ((k.nextQuack -= dt) <= 0) k.quack = 0.8f, k.nextQuack = frand(10, 30);
                if ((k.nextPreen -= dt) <= 0) k.preen = 1.2f, k.nextPreen = frand(15, 40);
            }
            if (k.role == DUCKLING && k.hp < maxHp(k)) k.hp += dt * 2;
        }
        if (s.havePet) {
            Duck& p = d[0];
            p.size  = petSize();
            p.hp    = min(maxHp(p), p.hp + dt * (s.energy > 30 ? 3 : 0.6f));
            s.energy = max(0.0f, s.energy - dt * 0.2f);
            int st = evo::stageFor(clock.evolveSecs);
            while (s.body.stage < st && s.body.stage < 5) {
                evo::lockStage(s.body, evo::tallyTraits(s.tally));
                if (s.body.stage == 5) s.body.locked[4] = s.body.locked[3];
                event((QuipEvent)(QE_STAGE2 + s.body.stage - 2));
                shimmer = 3;
                ledFlash(true, true, true, 500);
                store();
            }
            if (s.tally.fedMeals + s.tally.wildMeals >= 100) event(QE_MEALS100);
        }
        joy = max(0.0f, joy - dt), crownT = max(0.0f, crownT - dt), shimmer = max(0.0f, shimmer - dt);
        fights(dt);
        if (!s.havePet && !clock.paused) judgeRoundOne();  // PAUSED holds round one too
        if (now - saveMs > 60000) saveMs = now, store();
        blinkT += dt;
        if (blinkT > 4 + frand(0, 3)) blinkT = 0;
        quips(now, dt);
        readout(tt, lt, now);
        publish(t);
    }

    // ---------------------------------------------------------------- the words on screen
    void leaningText(char* out, int n) {
        evo::Traits t   = shape();
        const char* pos[] = {"SUNNY", "BOSSY", "CHUBBY"}, *neg[] = {"MOONLIT", "SWEET", "SLEEK"};
        float       v[3]  = {t.sun, t.hunt, t.plump};
        int         a = -1, b = -1;
        for (int i = 0; i < 3; i++) {
            if (fabsf(v[i]) < 0.15f) continue;
            if (a < 0 || fabsf(v[i]) > fabsf(v[a])) b = a, a = i;
            else if (b < 0 || fabsf(v[i]) > fabsf(v[b])) b = i;
        }
        char l[32] = "BALANCED";
        if (a >= 0 && b < 0) snprintf(l, sizeof l, "%s", v[a] > 0 ? pos[a] : neg[a]);
        if (a >= 0 && b >= 0) snprintf(l, sizeof l, "%s, %s", v[a] > 0 ? pos[a] : neg[a], v[b] > 0 ? pos[b] : neg[b]);
        snprintf(out, n, "LEANING: %s%s", petEvil() ? "EVIL, " : "", l);
    }
    void readout(time_t tt, const struct tm& lt, uint32_t now) {
        bool ok  = timeOk();
        int  h12 = lt.tm_hour % 12 ? lt.tm_hour % 12 : 12;
        int  day = s.born && ok ? (tt - s.born) / 86400 + 1 : 1;
        if (s.havePet) snprintf(hud, sizeof hud, "DAY %d  STAGE %d/5%s", day, s.body.stage, clock.paused ? "  PAUSED" : "");
        else snprintf(hud, sizeof hud, "ROUND ONE%s", clock.paused ? "  PAUSED" : "");
        if (s.havePet) leaningText(leaning, sizeof leaning);
        else leaning[0] = 0;
        if (crownT > 0) strcpy(banner[0], "THIS ONE IS YOURS"), banner[1][0] = 0;
        else if (bannerT <= 0) banner[0][0] = banner[1][0] = 0;
        if (deskClock) {
            if (ok) snprintf(bigTime, sizeof bigTime, "%d:%02d", h12, lt.tm_min);
            else strcpy(bigTime, "--:--");
            int tf, code, hi, lo;
            static const char* wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            static const char* mo[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            if ((now / 6000) % 2 && netWeather(tt, tf, code, hi, lo)) snprintf(info, sizeof info, "%d\xF8" "F %s", tf, netWeatherName(code));
            else if (ok) snprintf(info, sizeof info, "%s %s %d  %s", wd[lt.tm_wday], mo[lt.tm_mon], lt.tm_mday, lt.tm_hour < 12 ? "AM" : "PM");
            else info[0] = 0;
            if (ok && !frozen && lt.tm_hour == 4 && lt.tm_min == 0 && lt.tm_sec < 3 && millis() > 3600000UL) {
                store();
                netRequestSync();
            }
        } else {
            bigTime[0] = info[0] = 0;
            if (ok) snprintf(hud + strlen(hud), sizeof hud - strlen(hud), "   %d:%02d %s", h12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
        }
    }

    // ---------------------------------------------------------------- what the screen shows
    void publish(uint32_t t) {
        Snapshot& o = *snapWrite();
        o.kind = 1, o.tMs = evo::animMs(t), o.light = light, o.desk = deskClock, o.showBars = s.havePet;
        o.nCells = o.nAlgae = o.nBits = 0;
        o.hp01 = s.havePet ? d[0].hp / maxHp(d[0]) : 0, o.food01 = s.energy / 100;
        o.nDucks = 0;
        for (int i = 0; i < NDUCK && o.nDucks < 16; i++) {
            const Duck& k = d[i];
            if (!k.alive) continue;
            SnapDuck& q = o.ducks[o.nDucks++];
            q.x = k.x, q.y = k.y, q.size = k.size, q.face = k.face, q.bob = k.ph, q.dabble = k.dabble, q.flap = k.flap;
            q.alpha = k.alpha * (k.eaten ? max(0.0f, 1 - k.eaten * 1.4f) : 1), q.calm = k.calm;
            q.role  = k.role == DUCK_PET && crownT > 2 ? DUCK_CROWNED : k.role;
            q.stage = k.role != DUCKLING ? s.body.stage : 1, q.evil = k.evil;
            q.look  = (sleeping(k) ? LOOK_SLEEP : 0) | (k.startle > 0 ? LOOK_STARTLED : 0) | (k.quack > 0 ? LOOK_QUACK : 0) | (k.preen > 0 ? LOOK_PREEN : 0);
        }
        o.nPrey = 0;
        for (auto& c : prey)
            if (c.alive && o.nPrey < 8) o.prey[o.nPrey++] = {(int16_t)c.x, (int16_t)c.y, c.type, c.phase, c.depth};
        o.nFood = 0;
        for (auto& f : food)
            if (f.alive && o.nFood < 32) o.food[o.nFood][0] = f.x, o.food[o.nFood][1] = f.y, o.foodType[o.nFood++] = f.type;
        o.nSplat = 0;
        for (auto& p : splat)
            if (p.life > 0 && o.nSplat < 64) o.splat[o.nSplat++] = {(int16_t)p.x, (int16_t)p.y, p.red, p.life};
        o.turtleX = turtleX, o.turtleY = turtleY, o.turtleDir = turtleDir, o.splatterRed = splatterRed;
        o.pet       = {};
        o.pet.stage = s.havePet ? s.body.stage : 0, o.pet.blink = blinkT, o.pet.joy = joy, o.pet.shape = shape();
        o.pet.glow = glow, o.pet.shimmer = shimmer;
        memcpy(o.hud, hud, sizeof o.hud);
        memcpy(o.bigTime, bigTime, sizeof o.bigTime);
        memcpy(o.info, info, sizeof o.info);
        memcpy(o.leaning, leaning, sizeof o.leaning);
        memcpy(o.banner, banner, sizeof o.banner);
        o.bannerAge = crownT > 0 ? 4 - crownT : bannerAge;
        snapPublish();
    }

    // ---------------------------------------------------------------- touch
    void tap(int x, int y) {
        int hit = -1;
        for (int i = 0; i < NDUCK; i++) {
            const Duck& k = d[i];
            if (k.alive && !k.eaten && (k.x - x) * (k.x - x) + (k.y - k.size * 0.5f - y) * (k.y - k.size * 0.5f - y) < (k.size + 10) * (k.size + 10)) hit = i;
        }
        if (hit < 0 || !s.havePet) {  // the water (or round one, which is pure nature): peas and oats
            for (int n = 0; n < 5; n++) addFood(x + frand(-10, 10), y + frand(-8, 8), 1, true);
            return;
        }
        Duck& k = d[hit];
        if (k.role != DUCKLING) {  // your duck: a cheer
            joy = 1.2f, k.flap = 0.6f, k.quack = 0.8f;
            ledFlash(true, true, true, 120);
        } else if (k.evil) {  // an evil duckling: a bop
            event(QE_FIRST_BOP);
            k.hp -= maxHp(k) * 0.4f, k.flap = 0.6f, k.startle = 1;
            k.vx += (k.x - x) * 3, k.vy += (k.y - y) * 3;
            if (k.hp <= 0) spray(k.x, k.y - k.size * 0.6f, false, 14), k.eaten = 0.001f, k.eater = hit;  // a puff of dark feathers
        } else {  // a startled duckling: a quack, a flap, off it goes
            k.startle = 1, k.quack = 0.8f, k.flap = 0.6f;
            k.vx += (k.x - x) * 3, k.vy += (k.y - y) * 3;
        }
    }
};

Ducks* d;

}  // namespace

void duckBegin() {
    d = new Ducks();
    d->begin();
}
void duckStep(uint32_t nowMs) {
    if (!d->frozen) d->update(nowMs);
}
void       duckTap(int x, int y) { d->tap(x, y); }
void       duckSave() { d->store(); }
evo::Save& duckData() { return d->s; }
void       duckPause(bool on) {
    if (on) d->event(QE_FIRST_PAUSE);
    d->clock.paused = on;
    d->store();
}
void duckGenesis() {
    d->s = evo::freshSave(esp_random());
    d->newClutchFlags();
    d->clock.evolveSecs = 0, d->clock.paused = false;
    d->override = false, d->shimmer = 0, d->nQueued = 0, d->bannerT = 0, d->crownT = 0, d->fitBoost = 1, d->holdRound = false;
    d->startRoundOne();
    d->store();
}
void duckForce(float sun, float hunt, float plump) {
    d->override = true;
    d->forced   = {sun, hunt, plump};
}

void duckDebug(char c) {
    evo::Save& s = d->s;
    switch (c) {
        case '+': d->clock.evolveSecs += 6 * 3600; break;
        case 'n': d->clock.evolveSecs = (uint32_t)evo::stageFor(d->clock.evolveSecs) * evo::STAGE_SECS; break;
        case 'w': d->fitBoost = 20; break;
        case 'h': d->holdRound = !d->holdRound; break;
        case 'q': d->quipAt = d->nowMs(); break;
        case 'r': duckGenesis(); break;
        case 'N': d->forceNight = !d->forceNight; break;
        case 'X': {  // a bug, a frog, a goldfish and the turtle, right now (for looking)
            uint32_t now = d->nowMs();
            for (auto& at : d->preyAt) at = now;
            d->turtleDir = 1, d->turtleX = -50, d->turtleY = 150, d->turtleMs = now;
            break;
        }
        case 'E': {  // the duckling nearest your duck (or the middle) turns evil
            float bx = s.havePet ? d->d[0].x : CX, by = s.havePet ? d->d[0].y : CY, best = 1e9;
            int   pick = -1;
            for (int i = s.havePet ? 1 : 0; i < NDUCK; i++)
                if (d->d[i].alive && !d->d[i].evil) {
                    float q = (d->d[i].x - bx) * (d->d[i].x - bx) + (d->d[i].y - by) * (d->d[i].y - by);
                    if (q < best) best = q, pick = i;
                }
            if (pick >= 0) d->d[pick].evil = 1;
            break;
        }
        case 'V':  // your duck turns evil (or back), for looking at the villain
            if (s.havePet) d->flags.petEvil = !d->flags.petEvil, d->d[0].evil = d->flags.petEvil, d->store();
            break;
        case 'Z':
            d->frozen = !d->frozen;
            d->virt = d->virtStart = millis();
            d->last          = d->virt;
            d->epochAtFreeze = time(nullptr);
            break;
        case '.':
            if (d->frozen) d->virt += 50, d->update(d->virt);
            break;
        case 'K':
            if (s.havePet) d->d[0].x = CX, d->d[0].y = CY + 10, d->d[0].vx = d->d[0].vy = 0;
            break;
        case 't': {
            evo::Traits now = evo::tallyTraits(s.tally), shape = d->shape();
            int         evil = 0, alive = 0;
            for (auto& k : d->d) evil += k.alive && k.evil, alive += k.alive;
            Serial.printf("duck pet %d stage %d evolve %lus paused %d evilPet %d | ducks %d evil %d | now sun %.2f hunt %.2f plump %.2f | shape %.2f %.2f %.2f\n",
                          s.havePet, s.body.stage, (unsigned long)d->clock.evolveSecs, d->clock.paused, d->flags.petEvil, alive, evil, now.sun,
                          now.hunt, now.plump, shape.sun, shape.hunt, shape.plump);
            Serial.printf("tally light %.1f/%lu fights %lu wins %lu fed %lu wild %lu kills %lu energy %.0f\n", s.tally.lightSum,
                          (unsigned long)s.tally.lightN, (unsigned long)s.tally.fights, (unsigned long)s.tally.wins, (unsigned long)s.tally.fedMeals,
                          (unsigned long)s.tally.wildMeals, (unsigned long)s.kills, s.energy);
            for (int i = 0; i < evo::DROP; i++)
                if (!s.havePet)
                    Serial.printf("  drop %d alive %d evil %d fit %.1f meals %u wins %u\n", i, s.dropAlive[i], (d->flags.dropEvil >> i) & 1, s.dropFit[i],
                                  s.dropMeals[i], s.dropWins[i]);
            break;
        }
    }
}

}  // namespace cp
