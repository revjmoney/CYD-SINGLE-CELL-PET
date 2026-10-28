// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The drop, in screen pixels. Round one: eight random cells and no pet; the
// first to prove itself (meals, fights won, health) is crowned and becomes
// yours. From then on the pet evolves on the stage clock while its care
// (room light, fights, feeding) is tallied and locked into its body.
// Neglect never kills it: it forages alone and bursts away from lost fights.
#include "world.h"
#include <Arduino.h>
#include "led.h"
#include "net.h"
#include "quips.h"
#include "pet.h"
#include "save.h"
#include "snapshot.h"

namespace cp {

namespace {

constexpr int   W = SW, H = SH;
constexpr float CX = W / 2.0f, CY = H / 2.0f;
constexpr int   TOP = 18;  // the readout keeps the top rows

float frand(float a, float b) { return a + (b - a) * (esp_random() & 0xFFFF) / 65535.0f; }

// a genome's hue as a soft, lit colour
void hueColor(uint8_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
    float h = hue / 255.0f * 6, f = h - (int)h;
    float c[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
    float* k = c[(int)h % 6];
    r = 110 + 140 * k[0], g = 110 + 140 * k[1], b = 110 + 140 * k[2];
}

struct Cell {
    float       x, y, vx, vy, r, hp, heading, ph[3], wob, bop, eaten;  // eaten: 0 alive, >0 being swallowed
    uint8_t     role, alive, cr, cg, cb;
    int8_t      eater, drop;  // drop: which round-one genome, or -1
    evo::Traits lean;         // drop cells behave by their leanings
    float       speed;
    float       calm;  // after a fight: a few seconds of no damage, so a beaten cell can get away
};
struct Algae {
    float   x, y, vx, vy, rot;
    uint8_t n, alive, fed;  // fed: dropped by a person
};
struct Bit {  // debris from a meal, drawn into the winner
    float  x, y, vx, vy, life;
    int8_t to;
};

constexpr int NCELL = 13, NALG = 64, NBIT = 48;

struct World {
    Cell       c[NCELL];
    Algae      al[NALG];
    Bit        bits[NBIT];
    evo::Save  s;
    evo::Clock clock;
    float      light = 0.6f, joy = 0, blinkT = 0, tardX = -120, tardY = 140, tardDir = 1, tardStep = 0, crownT = 0;
    float      fitBoost = 1;  // serial `w`: round one, sped up for testing
    bool       holdRound = false;
    bool       override = false;  // serial F: forced traits, for looking at bodies
    evo::Traits forced{};
    float      shimmer = 0, spin = 0, pulse = 0, pulsePhase = 0, glow = 0;
    int        nTent = 0;
    float      tent[TENT][TSEG][2];
    bool       night = false;
    bool       frozen = false;  // serial Z: the pond only moves on serial '.', one 50 ms step each (for filming)
    uint32_t   virt   = 0, virtStart = 0;
    time_t     epochAtFreeze = 0;  // while frozen, the clock on screen moves with the steps too
    uint32_t   nowMs() const { return frozen ? virt : millis(); }
    uint32_t   last = 0, lightMs = 0, spawnMs = 0, saveMs = 0, tallyMs = 0;
    char       hud[48], bigTime[8], info[40], leaning[40], banner[2][40];
    float      bannerT = 0, bannerAge = 0;  // seconds left on screen, seconds shown
    uint32_t   quipAt = 0, rng = 0;  // when the next silly achievement pops up
    QuipEvent  queued[4];
    int        nQueued = 0;

    int  petIdx() const { return s.havePet ? 0 : -1; }
    bool timeOk() const { return time(nullptr) > 1700000000; }
    // what its body shows: the care locked in at each stage, weighted toward the latest; a jellyfish is final
    evo::Traits shape() const {
        if (override) return forced;
        return evo::shapeOf(s.body, s.body.stage >= 5 ? s.body.locked[4] : evo::tallyTraits(s.tally));
    }
    // the pet's body: plumper with feeding, bigger at each stage
    float petRadius() const {
        static const float grow[5] = {1.0f, 1.3f, 1.7f, 2.2f, 3.0f};
        int st = s.body.stage < 1 ? 1 : s.body.stage > 5 ? 5 : s.body.stage;
        return 16 * (0.85f + 0.3f * (shape().plump + 1) / 2) * grow[st - 1];
    }
    float maxHp(const Cell& k) const { return k.r * 10; }

    // ---------------------------------------------------------------- setting up
    void begin() {
        s = petLoad();
        if (evo::roundOneEmpty(s)) s = evo::freshSave(esp_random());  // an old drop with nobody left in it
        clock.evolveSecs = s.evolveSecs, clock.paused = s.paused;
        memset(c, 0, sizeof c);
        for (auto& a : al) a.alive = 0;
        for (int i = 0; i < 40; i++) newAlgae(frand(12, W - 12), frand(TOP + 8, H - 12), false);
        for (auto& b : bits) b.life = 0;
        if (s.havePet) {
            newCell(0, ROLE_PET);
            c[0].x = CX, c[0].y = CY;
            c[0].hp = max(1.0f, s.hp01 * maxHp(c[0]));
            for (int i = 1; i < 8; i++) newCell(i, ROLE_RIVAL);
            newCell(8, ROLE_HUNTER);
        } else
            startRoundOne();
        pinMode(34, INPUT);  // the light sensor on the front of the board
        last = saveMs = tallyMs = millis();
        rng    = esp_random();
        quipAt = last + quipDelayMs(rng);
    }
    void startRoundOne() {
        memset(c, 0, sizeof c);
        for (int i = 0; i < evo::DROP; i++)
            if (s.dropAlive[i]) {
                newCell(i, ROLE_DROP, i);
                c[i].x = frand(30, W - 30), c[i].y = frand(TOP + 30, H - 30);  // they start out spread through the drop
            }
    }

    void newCell(int i, uint8_t role, int drop = -1) {
        Cell& k = c[i];
        float a = frand(0, 6.283f);  // swim in from a random edge
        k       = {};
        k.role = role, k.alive = 1, k.eater = -1, k.drop = drop;
        k.x = CX + cosf(a) * (CX - 12), k.y = CY + sinf(a) * (CY - 12);
        k.heading = a + 3.14159f;
        for (auto& p : k.ph) p = frand(0, 6.283f);
        k.speed = 1;
        if (role == ROLE_PET) {
            hueColor(s.pet.hue, k.cr, k.cg, k.cb);
            k.r = petRadius(), k.lean = s.pet.lean, k.speed = s.pet.speed;
        } else if (role == ROLE_DROP) {
            const evo::Genome& g = s.drop[drop];
            hueColor(g.hue, k.cr, k.cg, k.cb);
            k.r = 14 * g.size, k.lean = g.lean, k.speed = g.speed;
        } else if (role == ROLE_RIVAL)
            k.r = 16 * frand(0.5f, 0.95f), k.cr = 230, k.cg = 150, k.cb = 190;
        else
            k.r = 16 * frand(1.15f, 1.35f), k.cr = 235, k.cg = 190, k.cb = 90;
        k.hp = maxHp(k);
    }
    void newAlgae(float x, float y, bool fed) {
        for (auto& a : al)
            if (!a.alive) {
                a = {x, y, frand(-2, 2), frand(-2, 2), frand(0, 6.283f), (uint8_t)(1 + (esp_random() % 3)), 1, fed};
                return;
            }
    }

    // ---------------------------------------------------------------- life
    void steer(Cell& k, float tx, float ty, float accel, float dt) {
        float dx = tx - k.x, dy = ty - k.y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
        k.vx += dx / d * accel * dt, k.vy += dy / d * accel * dt;
    }
    void think(int i, float dt, uint32_t t) {
        Cell& k     = c[i];
        float base  = k.role == ROLE_PET ? 56 : k.role == ROLE_DROP ? 40 : k.role == ROLE_RIVAL ? 36 : 26;
        float speed = base * k.speed * (k.hp < k.r * 3 ? 0.6f : 1) * (k.role == ROLE_PET && s.body.stage >= 5 ? 0.35f : 1);
        // the nearest algae, the nearest weaker cell, the nearest stronger one
        int   fa = -1, prey = -1, threat = -1;
        float da = 1e9, dp = 1e9, dt2 = 1e9;
        for (int j = 0; j < NALG; j++)
            if (al[j].alive) {
                float d = (al[j].x - k.x) * (al[j].x - k.x) + (al[j].y - k.y) * (al[j].y - k.y);
                if (d < da) da = d, fa = j;
            }
        for (int j = 0; j < NCELL; j++) {
            if (j == i || !c[j].alive || c[j].eaten) continue;
            float d = (c[j].x - k.x) * (c[j].x - k.x) + (c[j].y - k.y) * (c[j].y - k.y);
            if (c[j].r < k.r * 0.9f && d < dp) dp = d, prey = j;
            if (c[j].r > k.r * 1.15f && d < dt2) dt2 = d, threat = j;
        }
        // leanings: hunters chase, greedy ones graze harder, sun-lovers drift to the lamp (or away in the dark)
        bool  pet = k.role == ROLE_PET, drop = k.role == ROLE_DROP;
        float hunt = (pet || drop) ? k.lean.hunt : 0, appetite = 1 + 0.25f * (k.lean.plump + k.lean.hunt);
        bool  hungry = !pet || s.energy < 85;
        bool  willHunt = k.role == ROLE_HUNTER || (pet && s.energy > 35 && k.hp > maxHp(k) * 0.5f) || (drop && hunt > -0.2f);
        if (k.calm > 2 && threat < 0) threat = prey;  // just lost a fight: get away from the nearest cell
        if (threat >= 0 && (dt2 < (k.r * 5) * (k.r * 5) || k.calm > 2) && k.role != ROLE_HUNTER) {
            steer(k, 2 * k.x - c[threat].x, 2 * k.y - c[threat].y, speed * 3, dt);  // flee
        } else if (prey >= 0 && k.role != ROLE_RIVAL && dp < 120 * 120 && willHunt) {
            steer(k, c[prey].x, c[prey].y, speed * (2.4f + (drop ? hunt : 0)), dt);  // hunt
        } else if (fa >= 0 && hungry) {
            steer(k, al[fa].x, al[fa].y, speed * 2 * appetite, dt);  // graze
        } else {
            float w = k.ph[0] + evo::animMs(t) * 0.0004f;  // wander
            float sx = CX + cosf(w) * 110, sy = CY + sinf(w * 1.3f) * 76;
            if (drop || pet) {  // sun-lovers drift toward the lamp's bright middle when the room is lit
                float pull = k.lean.sun * (light - 0.5f);
                sx += (CX - sx) * pull, sy += (CY - sy) * pull;
            }
            steer(k, sx, sy, speed, dt);
        }
        // eat the algae it touches
        if (fa >= 0 && da < (k.r + 4) * (k.r + 4)) {
            bool fed     = al[fa].fed;
            al[fa].alive = 0;
            if (pet) {
                s.energy = min(100.0f, s.energy + 6), k.hp = min(maxHp(k), k.hp + 8);
                ledFlash(false, true, false, 80);
                if (!clock.paused) evo::addMeal(s.tally, fed);
            } else {
                k.hp = min(maxHp(k), k.hp + 4);
                if (drop && !clock.paused) s.dropMeals[k.drop]++;
            }
        }
    }

    void fight(float dt) {
        float strength = s.havePet ? 1 : 0.35f;  // round one is rough, but not a massacre
        for (int i = 0; i < NCELL; i++)
            for (int j = i + 1; j < NCELL; j++) {
                Cell &a = c[i], &b = c[j];
                if (!a.alive || !b.alive || a.eaten || b.eaten) continue;
                float dx = b.x - a.x, dy = b.y - a.y, d = sqrtf(dx * dx + dy * dy) + 1e-3f, o = a.r + b.r - d;
                if (o <= 0) continue;
                // push apart, squish, and both take damage (the bigger one deals more)
                a.vx -= dx / d * o * 6 * dt, a.vy -= dy / d * o * 6 * dt;
                b.vx += dx / d * o * 6 * dt, b.vy += dy / d * o * 6 * dt;
                a.wob = b.wob = 1;
                if (a.calm > 0 || b.calm > 0) continue;  // just bumping, no harm
                float pa = a.r * (a.role == ROLE_PET ? 1.3f + 0.3f * max(0.0f, a.lean.hunt) : 1);
                float pb = b.r * (b.role == ROLE_PET ? 1.3f + 0.3f * max(0.0f, b.lean.hunt) : 1);
                a.hp -= pb * 4 * dt * strength, b.hp -= pa * 4 * dt * strength;
                if (a.hp <= 0) lose(i, j);
                else if (b.hp <= 0) lose(j, i);
            }
    }
    void lose(int loser, int winner) {
        Cell& l = c[loser];
        // in round one the contenders only bruise each other: the loser bursts away and the win is counted,
        // so all eight stay in it for hours instead of the drop emptying out in minutes
        if (l.role == ROLE_DROP) {
            l.hp = maxHp(l) * 0.3f;
            l.vx = (l.x - c[winner].x) * 4, l.vy = (l.y - c[winner].y) * 4;
            l.calm = 4, c[winner].calm = 2;
            if (c[winner].role == ROLE_DROP && !clock.paused) s.dropWins[c[winner].drop]++;
            return;
        }
        if (l.role == ROLE_PET) {  // your cell never dies: it bursts away hurt and recovers
            l.hp = 1, l.calm = 4, c[winner].calm = 2;
            l.vx = (l.x - c[winner].x) * 4, l.vy = (l.y - c[winner].y) * 4;
            s.energy = max(0.0f, s.energy - 20);
            if (!clock.paused) evo::addFight(s.tally, false);
            return;
        }
        l.eaten = 0.001f, l.eater = winner;
        if (l.role == ROLE_DROP) s.dropAlive[l.drop] = 0;  // that genome is gone
        Cell& w = c[winner];
        if (w.role == ROLE_PET) {
            ledFlash(true, false, false, 250);
            event(QE_CANNIBAL);
            s.kills++;
            s.energy = min(100.0f, s.energy + 25);
            joy      = 1.5f;
            if (!clock.paused) evo::addFight(s.tally, true);
        } else if (w.role == ROLE_DROP && !clock.paused)
            s.dropWins[w.drop]++;
    }

    // ---------------------------------------------------------------- the jellyfish
    // It swims by pulses toward food, catches algae and small cells on its
    // tentacles, and at night (dark room, late) settles low and pulses slowly.
    void jelly(float dt) {
        Cell&       k  = c[0];
        evo::Traits sh = shape();
        float period   = night ? 4.0f : 1.6f;
        pulsePhase += dt / period;
        float fx = cosf(k.heading), fy = sinf(k.heading), sx = -fy, sy = fx;
        if (pulsePhase >= 1) {
            pulsePhase -= 1;
            float push = night ? 25 : 60;
            k.vx += fx * push, k.vy += fy * push;
        }
        pulse = pulsePhase < 0.25f ? sinf(pulsePhase / 0.25f * 1.5708f) : 1 - (pulsePhase - 0.25f) / 0.75f;
        if (night) k.vy += (H * 0.78f - k.y) * 0.6f * dt;  // settle toward the bottom
        // tentacles: chains hanging back from the bell's margin, lagging and swaying
        nTent = 8 + (int)(6 * max(0.0f, sh.hunt) + 2 * max(0.0f, sh.plump));
        nTent = nTent < 6 ? 6 : nTent > TENT ? TENT : nTent;
        float aspect = 1.0f + 0.25f * (sh.plump + 1), V = k.r * aspect * 0.75f * (1 - 0.2f * pulse);
        float len = k.r * (1.4f + 0.8f * max(0.0f, sh.hunt)), seg = len / (TSEG - 1);
        for (int i = 0; i < nTent; i++) {
            float v = (nTent > 1 ? i / (nTent - 1.0f) - 0.5f : 0) * 2 * V * 0.85f;
            tent[i][0][0] = k.x + sx * v - fx, tent[i][0][1] = k.y + sy * v - fy;
            for (int q = 1; q < TSEG; q++) {
                float sway = sinf(spin * 3 + i * 0.7f + q * 0.6f) * 3;
                float tx = tent[i][q - 1][0] - fx * seg + sx * sway, ty = tent[i][q - 1][1] - fy * seg + sy * sway;
                float a = min(1.0f, 6 * dt);
                tent[i][q][0] += (tx - tent[i][q][0]) * a, tent[i][q][1] += (ty - tent[i][q][1]) * a;
                float dx = tent[i][q][0] - tent[i][q - 1][0], dy = tent[i][q][1] - tent[i][q - 1][1];
                float d  = sqrtf(dx * dx + dy * dy) + 1e-3f;
                if (d > seg) tent[i][q][0] = tent[i][q - 1][0] + dx / d * seg, tent[i][q][1] = tent[i][q - 1][1] + dy / d * seg;
            }
        }
        // whatever brushes the tentacles gets caught: algae, and cells small enough
        for (int i = 0; i < nTent; i++)
            for (int q = 2; q < TSEG; q++) {
                float px = tent[i][q][0], py = tent[i][q][1];
                for (auto& a : al)
                    if (a.alive && fabsf(a.x - px) < 6 && fabsf(a.y - py) < 6) {
                        a.alive  = 0;
                        s.energy = min(100.0f, s.energy + 6), k.hp = min(maxHp(k), k.hp + 8);
                        ledFlash(false, true, false, 80);
                        if (!clock.paused) evo::addMeal(s.tally, a.fed);
                    }
                for (int j = 1; j < NCELL; j++) {
                    Cell& o = c[j];
                    if (!o.alive || o.eaten || o.r > k.r * 0.6f) continue;
                    if ((o.x - px) * (o.x - px) + (o.y - py) * (o.y - py) < (o.r + 2) * (o.r + 2)) {
                        o.hp -= 40 * dt, o.bop = 0.3f;
                        if (o.hp <= 0) lose(j, 0);
                    }
                }
            }
    }

    // ---------------------------------------------------------------- round one
    void judgeRoundOne() {
        int alive = 0, last = -1, best = -1;
        float bestFit = 0;
        for (int i = 0; i < NCELL; i++) {
            const Cell& k = c[i];
            if (!k.alive || k.eaten || k.role != ROLE_DROP) continue;
            alive++, last = i;
            // meals in 64ths, wins in 48ths: measured rates put the best cell about an hour from the crown
            float f = evo::fitness(s.dropMeals[k.drop] / 64, s.dropWins[k.drop] / 48, k.hp / maxHp(k)) * fitBoost;
            s.dropFit[k.drop] = f;
            if (f > bestFit) bestFit = f, best = i;
        }
        if (holdRound) return;  // serial `h`: round one runs on, nobody is crowned (for measuring)
        if (bestFit >= evo::WIN_FITNESS) crown(best);
        else if (alive == 1) crown(last);  // the last one standing wins by default
    }
    void crown(int i) {
        Cell& k = c[i];
        s.pet   = s.drop[k.drop];
        s.havePet = 1, s.energy = 80;
        // its inborn leanings are where its care starts
        for (int n = 0; n < 10; n++) evo::addLight(s.tally, (k.lean.sun + 1) / 2, 12, false);
        int wins = (k.lean.hunt + 1) * 5, fed = (k.lean.plump + 1) * 5;
        for (int n = 0; n < 10; n++) evo::addFight(s.tally, n < wins);
        for (int n = 0; n < 10; n++) evo::addMeal(s.tally, n < fed);
        // the winner takes slot 0, everyone else carries on as ordinary pond life
        Cell winner = k;
        winner.role = ROLE_PET, winner.drop = -1, winner.lean = s.pet.lean, winner.speed = s.pet.speed;
        winner.r = petRadius(), winner.hp = maxHp(winner);
        for (int j = 0; j < NCELL; j++)
            if (j != i && c[j].alive && c[j].role == ROLE_DROP) c[j].role = ROLE_RIVAL, c[j].drop = -1;
        if (i != 0) c[i] = c[0];
        c[0] = winner;
        if (c[i].alive && c[i].role == ROLE_DROP) c[i].role = ROLE_RIVAL;
        for (int j = 0; j < NCELL; j++)
            if (c[j].eater == i) c[j].eater = 0;
            else if (c[j].eater == 0 && i != 0) c[j].eater = i;
        crownT  = 4, joy = 2, fitBoost = 1;
        event(QE_CROWNED);
        ledFlash(true, true, true, 600);
        store();
    }

    // ---------------------------------------------------------------- silly achievements
    // an event line shows once per pet; it waits its turn if a banner is up
    void event(QuipEvent e) {
        if (!quipEventOnce(s.quipDone, e)) return;
        if (nQueued < 4) queued[nQueued++] = e;
    }
    void show(const char* line) {
        if (!quipWrap(line, banner[0], banner[1])) return;
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
        if (crownT > 0) return;  // the crowning has the screen
        if (nQueued) {
            if (const char* q = quipEvent(cellBook, queued[0])) show(q);
            memmove(queued, queued + 1, --nQueued * sizeof queued[0]);
            return;
        }
        if ((int32_t)(now - quipAt) < 0) return;
        int pool = quipPool(s.body.stage, s.havePet, false);
        if (pool != s.quipPool) s.quipPool = pool, s.quipOrder = 0, s.quipPos = 0;  // a new stage: a new pool
        show(quipPick(cellBook, pool, s.quipOrder, s.quipPos, rng));
        quipAt = now + quipDelayMs(rng);
    }

    // ---------------------------------------------------------------- saving
    void store() {
        s.evolveSecs = clock.evolveSecs, s.paused = clock.paused;
        if (s.havePet) s.hp01 = c[0].hp / maxHp(c[0]);
        petStore(s);
    }

    // ---------------------------------------------------------------- one step
    void update(uint32_t t) {
        uint32_t now = nowMs();
        float    dt  = min(0.05f, (now - last) / 1000.0f);
        last         = now;
        evo::tick(clock, now);

        // light sensor: brighter room, brighter microscope lamp, faster algae
        if (now - lightMs > 250) {
            lightMs = now;
            int   raw = analogRead(34);  // higher in the dark on this board
            float l   = 1.0f - min(raw, 3000) / 3000.0f;
            light += (0.25f + 0.75f * l - light) * 0.1f;
        }
        time_t    tt = frozen ? epochAtFreeze + (time_t)((virt - virtStart) / 1000) : time(nullptr);
        struct tm lt;
        localtime_r(&tt, &lt);
        if (!s.born && timeOk()) s.born = tt, store();  // the birthday, once the clock is known
        if (now - tallyMs > 10000) {
            tallyMs = now;
            if (s.havePet && !clock.paused) evo::addLight(s.tally, light, lt.tm_hour, timeOk());
        }

        // algae grows with the light
        if (now - spawnMs > 1000 / (0.3f + light)) {
            spawnMs = now;
            newAlgae(frand(12, W - 12), frand(TOP + 8, H - 12), false);
        }

        int wild = 0;
        for (int i = 0; i < NCELL; i++) wild += c[i].alive && (c[i].role == ROLE_RIVAL || c[i].role == ROLE_HUNTER);
        for (int i = 0; i < NCELL; i++) {
            Cell& k = c[i];
            if (!k.alive) {
                // new wanderers swim in now and then: up to 4 in round one, then 7 rivals and a hunter
                int  cap   = s.havePet ? 8 : 4;
                bool slotOk = s.havePet ? i >= 1 && i <= 8 : i >= evo::DROP;
                if (slotOk && wild < cap && frand(0, 1) < dt * 0.15f) newCell(i, s.havePet && i == 8 ? ROLE_HUNTER : ROLE_RIVAL), wild++;
                continue;
            }
            if (k.eaten) {  // being swallowed: shrink into the winner
                Cell& w = c[k.eater];
                k.eaten += dt;
                k.x += (w.x - k.x) * 4 * dt, k.y += (w.y - k.y) * 4 * dt;
                k.r *= 1 - 1.6f * dt;
                if (k.eaten > 0.7f || k.r < 2) {
                    for (int n = 0; n < 10; n++) spawnBit(k.x, k.y, k.eater);
                    if (w.role != ROLE_PET) w.r = min(w.r + 0.6f, 36.0f);
                    k.alive = 0;
                }
                continue;
            }
            if (k.calm > 0) k.calm -= dt;
            think(i, dt, t);
            // soft walls: nobody camps in a corner
            float m = k.r + 24;
            if (k.x < m) k.vx += (m - k.x) * 3 * dt;
            if (k.x > W - m) k.vx -= (k.x - (W - m)) * 3 * dt;
            if (k.y < TOP + m) k.vy += (TOP + m - k.y) * 3 * dt;
            if (k.y > H - m) k.vy -= (k.y - (H - m)) * 3 * dt;
            // cilia drag, a little drift
            k.vx *= 1 - 2.2f * dt, k.vy *= 1 - 2.2f * dt;
            k.vx += frand(-12, 12) * dt, k.vy += frand(-12, 12) * dt;
            k.x += k.vx * dt, k.y += k.vy * dt;
            float sp = sqrtf(k.vx * k.vx + k.vy * k.vy);
            if (sp > 4) {
                float want = atan2f(k.vy, k.vx), dh = want - k.heading;
                while (dh > 3.14159f) dh -= 6.283f;
                while (dh < -3.14159f) dh += 6.283f;
                k.heading += dh * min(1.0f, 4 * dt);
            }
            // the edges of the drop
            float lo = k.r + 2, top = k.r + TOP;
            if (k.x < lo) k.x = lo, k.vx = fabsf(k.vx) * 0.4f;
            if (k.x > W - lo) k.x = W - lo, k.vx = -fabsf(k.vx) * 0.4f;
            if (k.y < top) k.y = top, k.vy = fabsf(k.vy) * 0.4f;
            if (k.y > H - lo) k.y = H - lo, k.vy = -fabsf(k.vy) * 0.4f;
            k.wob = max(0.0f, k.wob - dt * 1.5f), k.bop = max(0.0f, k.bop - dt * 3);
            if (k.role != ROLE_PET && k.hp < maxHp(k)) k.hp += dt * 2;  // others heal slowly
        }
        if (s.havePet) {
            Cell& p = c[0];
            p.r     = petRadius();
            p.hp    = min(maxHp(p), p.hp + dt * (s.energy > 30 ? 3 : 0.6f));
            s.energy = max(0.0f, s.energy - dt * 0.2f);
            // evolving: at each stage change the care so far is locked into the body
            int st = evo::stageFor(clock.evolveSecs);
            while (s.body.stage < st && s.body.stage < 5) {
                evo::lockStage(s.body, evo::tallyTraits(s.tally));
                if (s.body.stage == 5) s.body.locked[4] = s.body.locked[3];  // the jellyfish is what it was raised as, for good
                event((QuipEvent)(QE_STAGE2 + s.body.stage - 2));
                shimmer = 3;
                ledFlash(true, true, true, 500);
                store();
            }
            night = timeOk() && light < 0.3f && (lt.tm_hour >= 22 || lt.tm_hour < 7);
            glow += ((night ? 1.0f : 0.0f) - glow) * min(1.0f, dt * 0.5f);
            if (s.body.stage >= 5) jelly(dt);
        }
        shimmer = max(0.0f, shimmer - dt), spin += dt * 0.4f;
        joy = max(0.0f, joy - dt), crownT = max(0.0f, crownT - dt);
        fight(dt);
        if (!s.havePet && !clock.paused) judgeRoundOne();  // PAUSED holds round one too
        if (now - saveMs > 60000) saveMs = now, store();

        for (auto& a : al)
            if (a.alive) {
                a.x += a.vx * dt, a.y += a.vy * dt, a.rot += dt * 0.3f;
                if (a.x < 8 || a.x > W - 8) a.vx = -a.vx;
                if (a.y < TOP || a.y > H - 8) a.vy = -a.vy;
            }
        for (auto& b : bits)
            if (b.life > 0) {
                Cell& w = c[b.to];
                b.vx += (w.x - b.x) * 6 * dt, b.vy += (w.y - b.y) * 6 * dt;
                b.vx *= 1 - 3 * dt, b.vy *= 1 - 3 * dt;
                b.x += b.vx * dt, b.y += b.vy * dt, b.life -= dt;
            }

        // the tardigrade: a slow walk across the drop now and then
        tardStep += dt;
        tardX += tardDir * 8 * dt;
        tardY += sinf(tardStep * 0.4f) * 6 * dt;
        if (tardX > W + 120 || tardX < -120) {
            if (frand(0, 1) < dt * 0.05f) tardDir = -tardDir, tardY = frand(80, 160);
        }
        for (auto& a : al)  // it grazes too
            if (a.alive && fabsf(a.x - tardX) < 16 && fabsf(a.y - tardY) < 10) a.alive = 0;

        blinkT += dt;
        if (blinkT > 4 + frand(0, 3)) blinkT = 0;
        if (s.havePet && s.tally.fedMeals + s.tally.wildMeals >= 100) event(QE_MEALS100);
        quips(now, dt);
        readout(tt, lt, now);
        publish(t);
    }
    void spawnBit(float x, float y, int to) {
        for (auto& b : bits)
            if (b.life <= 0) {
                b = {x + frand(-4, 4), y + frand(-4, 4), frand(-60, 60), frand(-60, 60), frand(0.5f, 1.0f), (int8_t)to};
                return;
            }
    }

    // ---------------------------------------------------------------- the words on screen
    void readout(time_t tt, const struct tm& lt, uint32_t now) {
        bool ok  = timeOk();
        int  h12 = lt.tm_hour % 12 ? lt.tm_hour % 12 : 12;
        int  day = s.born && ok ? (tt - s.born) / 86400 + 1 : 1;
        if (s.havePet) snprintf(hud, sizeof hud, "DAY %d  STAGE %d/5%s", day, s.body.stage, clock.paused ? "  PAUSED" : "");
        else snprintf(hud, sizeof hud, "ROUND ONE%s", clock.paused ? "  PAUSED" : "");
        if (s.havePet) {
            char l[32];
            evo::leaningText(shape(), l, sizeof l);
            snprintf(leaning, sizeof leaning, "LEANING: %s", l);
        } else
            leaning[0] = 0;
        if (crownT > 0) strcpy(banner[0], "THIS ONE IS YOURS"), banner[1][0] = 0;
        else if (bannerT <= 0) banner[0][0] = banner[1][0] = 0;
        if (deskClock) {
            if (ok) snprintf(bigTime, sizeof bigTime, "%d:%02d", h12, lt.tm_min);
            else strcpy(bigTime, "--:--");
            int tf, code, hi, lo;
            static const char* wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            static const char* mo[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            if ((now / 6000) % 2 && netWeather(tt, tf, code, hi, lo))
                snprintf(info, sizeof info, "%d\xF8" "F %s", tf, netWeatherName(code));
            else if (ok)
                snprintf(info, sizeof info, "%s %s %d  %s", wd[lt.tm_wday], mo[lt.tm_mon], lt.tm_mday, lt.tm_hour < 12 ? "AM" : "PM");
            else
                info[0] = 0;
            // the nightly resync, like the desk clock (the pet is saved first)
            if (ok && lt.tm_hour == 4 && lt.tm_min == 0 && lt.tm_sec < 3 && millis() > 3600000UL) {
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
        o.tMs = evo::animMs(t), o.light = light, o.desk = deskClock, o.showBars = s.havePet;
        o.kind = 0, o.nDucks = o.nPrey = o.nFood = o.nSplat = 0;
        o.hp01 = s.havePet ? c[0].hp / maxHp(c[0]) : 0, o.food01 = s.energy / 100;
        o.nCells = 0;
        for (int i = 0; i < NCELL && o.nCells < 16; i++) {
            const Cell& k = c[i];
            if (!k.alive) continue;
            SnapCell& q = o.cells[o.nCells++];
            q.x = k.x, q.y = k.y, q.r = k.r, q.heading = k.heading, q.wob = k.wob, q.bop = k.bop, q.eaten = k.eaten;
            memcpy(q.ph, k.ph, sizeof q.ph);
            q.cr = k.cr, q.cg = k.cg, q.cb = k.cb;
            q.role = k.role == ROLE_PET && crownT > 2 ? ROLE_CROWNED : k.role;
        }
        o.nAlgae = 0;
        for (auto& a : al)
            if (a.alive && o.nAlgae < 64) o.algae[o.nAlgae++] = {(int16_t)a.x, (int16_t)a.y, a.rot, a.n, a.fed};
        o.nBits = 0;
        for (auto& b : bits)
            if (b.life > 0 && o.nBits < 48) o.bits[o.nBits][0] = b.x, o.bits[o.nBits][1] = b.y, o.nBits++;
        o.tardX = tardX, o.tardY = tardY, o.tardDir = tardDir;
        o.pet       = {};
        o.pet.stage = s.havePet ? s.body.stage : 0, o.pet.blink = blinkT, o.pet.joy = joy;
        o.pet.shape = shape();
        o.pet.pulse = pulse, o.pet.glow = glow, o.pet.shimmer = shimmer, o.pet.spin = spin;
        o.pet.nTent = s.havePet && s.body.stage >= 5 ? nTent : 0;
        for (int i = 0; i < o.pet.nTent; i++)
            for (int q = 0; q < TSEG; q++) o.pet.tent[i][q][0] = tent[i][q][0], o.pet.tent[i][q][1] = tent[i][q][1];
        // a hunting past (stage 1 or 2) leaves two stinging threads, whatever it became
        bool past = s.body.stage > 2 && (s.body.locked[0].hunt > 0.3f || s.body.locked[1].hunt > 0.3f);
        for (int i = 0; i < TENT; i++) o.pet.sting[i] = past && i < 2;
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
        for (int i = 0; i < NCELL; i++)
            if (c[i].alive && !c[i].eaten && (c[i].x - x) * (c[i].x - x) + (c[i].y - y) * (c[i].y - y) < (c[i].r + 8) * (c[i].r + 8)) hit = i;
        if (!s.havePet || hit < 0) {  // round one is pure nature: a tap only ever feeds the drop
            for (int n = 0; n < 4; n++) newAlgae(x + frand(-8, 8), y + frand(-8, 8), true);
        } else if (hit == 0) {
            joy = 1.2f, c[0].wob = 1;
            ledFlash(true, true, true, 120);
        } else {
            Cell& k = c[hit];
            event(QE_FIRST_BOP);
            k.hp -= k.r * 4, k.bop = 1, k.wob = 1;
            k.vx += (k.x - c[0].x) * 2, k.vy += (k.y - c[0].y) * 2;
            if (k.hp <= 0) lose(hit, 0);
        }
    }
};

World* w;

}  // namespace

void worldBegin() {
    w = new World();
    w->begin();
}
void worldStep(uint32_t nowMs) {
    if (!w->frozen) w->update(nowMs);
}
void       worldTap(int x, int y) { w->tap(x, y); }
void       worldSave() { w->store(); }
evo::Save& worldData() { return w->s; }

void worldPause(bool on) {
    if (on) w->event(QE_FIRST_PAUSE);
    w->clock.paused = on;
    w->store();
}
void worldGenesis() {
    w->s = evo::freshSave(esp_random());
    w->clock.evolveSecs = 0, w->clock.paused = false;
    w->override = false, w->shimmer = 0, w->nTent = 0;
    w->startRoundOne();
    w->store();
}

void worldForce(float sun, float hunt, float plump) {
    w->override = true;
    w->forced   = {sun, hunt, plump};
}

void worldDebug(char c) {
    evo::Save& s = w->s;
    switch (c) {
        case '+': w->clock.evolveSecs += 6 * 3600; break;
        case 'n': w->clock.evolveSecs = (uint32_t)evo::stageFor(w->clock.evolveSecs) * evo::STAGE_SECS; break;
        case 'w': w->fitBoost = 20; break;
        case 'h': w->holdRound = !w->holdRound; break;
        case 'q': w->quipAt = w->nowMs(); break;
        case 'Z':
            w->frozen = !w->frozen;
            w->virt = w->virtStart = millis();
            w->last          = w->virt;
            w->epochAtFreeze = time(nullptr);
            break;
        case 'K':  // the pet to the middle of the drop (for filming)
            if (s.havePet) w->c[0].x = SW / 2, w->c[0].y = SH / 2 + 20, w->c[0].vx = w->c[0].vy = 0;
            break;
        case '.':
            if (w->frozen) w->virt += 50, w->update(w->virt);
            break;
        case 'r': worldGenesis(); break;
        case 't': {
            evo::Traits now = evo::tallyTraits(s.tally), shape = evo::shapeOf(s.body, now);
            Serial.printf("pet %d stage %d evolve %lus paused %d | now sun %.2f hunt %.2f plump %.2f | shape %.2f %.2f %.2f\n", s.havePet,
                          s.body.stage, (unsigned long)w->clock.evolveSecs, w->clock.paused, now.sun, now.hunt, now.plump, shape.sun,
                          shape.hunt, shape.plump);
            Serial.printf("tally light %.1f/%lu fights %lu wins %lu fed %lu wild %lu kills %lu born %lld\n", s.tally.lightSum,
                          (unsigned long)s.tally.lightN, (unsigned long)s.tally.fights, (unsigned long)s.tally.wins,
                          (unsigned long)s.tally.fedMeals, (unsigned long)s.tally.wildMeals, (unsigned long)s.kills, (long long)s.born);
            for (int i = 0; i < evo::DROP; i++)
                if (!s.havePet)
                    Serial.printf("  drop %d alive %d fit %.1f meals %u wins %u\n", i, s.dropAlive[i], s.dropFit[i], s.dropMeals[i], s.dropWins[i]);
            break;
        }
    }
}

}  // namespace cp
