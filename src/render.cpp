// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The microscope picture. Each strip is built in 8-bit RGB working rows with
// the same soft additive and alpha blending the pet has always used, then
// packed to RGB565 and sent by DMA while the next strip is drawn. Everything
// is drawn per strip: an object only costs time in the strips it touches.
#include "render.h"
#include <Arduino.h>
#include "board.h"
#include "pet.h"

namespace cp {

// ---------------------------------------------------------------- snapshots
static Snapshot*         snaps;  // three, from the heap when the pet starts (inside CYD Demoscene the demos keep that memory)
static int               snapFront = 0, snapReading = -1, snapWriting = 1;
static volatile uint32_t snapSeq;
static portMUX_TYPE      snapMux = portMUX_INITIALIZER_UNLOCKED;

Snapshot* snapWrite() {
    portENTER_CRITICAL(&snapMux);
    for (int i = 0; i < 3; i++)
        if (i != snapFront && i != snapReading) snapWriting = i;
    portEXIT_CRITICAL(&snapMux);
    return &snaps[snapWriting];
}
void snapPublish() {
    portENTER_CRITICAL(&snapMux);
    snapFront = snapWriting;
    snapSeq++;
    portEXIT_CRITICAL(&snapMux);
}
const Snapshot* snapAcquire(uint32_t& seq) {
    portENTER_CRITICAL(&snapMux);
    snapReading = snapFront;
    seq         = snapSeq;
    portEXIT_CRITICAL(&snapMux);
    return &snaps[snapReading];
}
void snapRelease() {
    portENTER_CRITICAL(&snapMux);
    snapReading = -1;
    portEXIT_CRITICAL(&snapMux);
}

namespace {

// ---------------------------------------------------------------- one strip of working rows
// both cores draw strips at once, so each task has its own working rows
__thread uint8_t *R, *G, *B;
__thread int      Y0;  // top screen line of the strip being drawn
uint8_t*          work[2][3];  // the working rows of core 0's and core 1's drawing tasks
void              useWork(int k) { R = work[k][0], G = work[k][1], B = work[k][2]; }
uint8_t* vig;  // LW x LH: the lamp, at half size: brightest in the middle, darker toward the edges
uint8_t (*atanQ)[128];  // 128 x 128: first-quadrant angle in 64ths of a turn (0..16), by |dy|, |dx|

float* sinTab;  // 1025: one turn of sine, for fsin/fcos
inline float fsin(float a) {
    float f = a * (1024 / 6.2831853f);
    int   i = (int)floorf(f);
    float r = f - i;
    i &= 1023;
    return sinTab[i] + (sinTab[i + 1] - sinTab[i]) * r;
}
inline float fcos(float a) { return fsin(a + 1.5707963f); }

// fast square root: one Newton step on the bit trick, plenty for sub-pixel edges
inline float fsqrt(float x) {
    if (x <= 0) return 0;
    union {
        float    f;
        uint32_t i;
    } u{x};
    u.i     = 0x5f3759df - (u.i >> 1);
    float y = u.f;
    y *= 1.5f - 0.5f * x * y * y;
    return x * y;
}

inline bool rowIn(int y) { return (unsigned)(y - Y0) < (unsigned)STRIP; }
inline void add(int x, int y, int r, int g, int b) {
    if ((unsigned)x >= (unsigned)SW || !rowIn(y)) return;
    int i = (y - Y0) * SW + x, v;
    v = R[i] + r, R[i] = v > 255 ? 255 : v < 0 ? 0 : v;
    v = G[i] + g, G[i] = v > 255 ? 255 : v < 0 ? 0 : v;
    v = B[i] + b, B[i] = v > 255 ? 255 : v < 0 ? 0 : v;
}
// a = 0..256 toward colour
inline void mix(int x, int y, int r, int g, int b, int a) {
    if ((unsigned)x >= (unsigned)SW || !rowIn(y) || a <= 0) return;
    if (a > 256) a = 256;
    int i = (y - Y0) * SW + x;
    R[i] += (r - R[i]) * a >> 8;
    G[i] += (g - G[i]) * a >> 8;
    B[i] += (b - B[i]) * a >> 8;
}
// does [cy - ext, cy + ext] touch this strip?
inline bool touches(float cy, float ext) { return cy + ext >= Y0 && cy - ext < Y0 + STRIP; }

// a soft round spot: full colour (alpha a) inside r, fading over soft pixels
void spot(float cx, float cy, float r, int cr, int cg, int cb, int a, float soft = 2.4f) {
    float ext = r + soft + 1;
    if (!touches(cy, ext)) return;
    int   x0 = cx - ext, x1 = cx + ext, y0 = max((int)(cy - ext), Y0), y1 = min((int)(cy + ext), Y0 + STRIP - 1);
    float in2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0, out2 = (r + soft) * (r + soft);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d2 >= out2) continue;
            if (d2 <= in2) {
                mix(x, y, cr, cg, cb, a);
                continue;
            }
            float d = fsqrt(d2) - r;
            mix(x, y, cr, cg, cb, d <= 0 ? a : (int)(a * (1 - d / soft)));
        }
}
void glowDot(float x, float y, int r, int g, int b) {
    if (!touches(y, 3)) return;
    int ix = (int)x, iy = (int)y;
    add(ix, iy, r, g, b), add(ix + 1, iy, r, g, b), add(ix, iy + 1, r, g, b), add(ix + 1, iy + 1, r, g, b);
    for (int k = 0; k < 2; k++) {
        add(ix - 1, iy + k, r / 3, g / 3, b / 3), add(ix + 2, iy + k, r / 3, g / 3, b / 3);
        add(ix + k, iy - 1, r / 3, g / 3, b / 3), add(ix + k, iy + 2, r / 3, g / 3, b / 3);
    }
}
// angle -> 0..63: a table lookup near the middle, the octant trick further out
inline int angle64f(float dx, float dy) {
    float ax = fabsf(dx), ay = fabsf(dy), a = ax > ay ? ay / (ax + 1e-6f) : ax / (ay + 1e-6f);
    float o = a * 8 * (1.0f - 0.22f * (1 - a));
    if (ay > ax) o = 16 - o;
    if (dx < 0) o = 32 - o;
    if (dy < 0) o = 64 - o;
    return ((int)o) & 63;
}
inline int angle64(int dx, int dy) {
    int ax = abs(dx), ay = abs(dy);
    if (ax < 128 && ay < 128) {
        int o = atanQ[ay][ax];
        if (dx < 0) o = 32 - o;
        if (dy < 0) o = 64 - o;
        return o & 63;
    }
    return angle64f(dx, dy);
}

// ---------------------------------------------------------------- the drop
void background(const Snapshot& s) {
    // dark teal water, brighter in the middle, lamp level from the room
    int lr = 10 + 18 * s.light, lg = 40 + 50 * s.light, lb = 46 + 50 * s.light;
    for (int yy = 0; yy < STRIP; yy++) {
        const uint8_t* v = vig + ((Y0 + yy) >> 1) * LW;
        uint8_t *     r = R + yy * SW, *g = G + yy * SW, *b = B + yy * SW;
        for (int x = 0; x < LW; x++) {
            int k = v[x];
            r[2 * x] = r[2 * x + 1] = lr * k >> 8;
            g[2 * x] = g[2 * x + 1] = lg * k >> 8;
            b[2 * x] = b[2 * x + 1] = lb * k >> 8;
        }
    }
    // out-of-focus specks drifting at another depth
    uint32_t t = s.tMs;
    for (int k = 0; k < 14; k++) {
        float sx = fmodf(k * 74.6f + t * 0.004f * (1 + k % 3), SW + 40) - 20;
        float sy = fmodf(k * 47.4f + fsin(t * 0.0003f + k) * 12 + k * 18, SH);
        spot(sx, sy, 2 + 2 * (k % 3), 120, 170, 160, 30, 5);
    }
}

void drawAlgae(const SnapAlgae& a) {
    // a short chain of round green cells with bright rims
    if (!touches(a.y, a.n * 6.4f + 6)) return;
    int cr = a.fed ? 80 : 60, cg = a.fed ? 190 : 170;
    for (int k = 0; k < a.n; k++) {
        float x = a.x + fcos(a.rot) * k * 6.4f, y = a.y + fsin(a.rot) * k * 6.4f;
        spot(x, y, 2.8f, cr, cg, 50, 170, 2);
        glowDot(x - 1, y - 1.2f, 40, 70, 20);
    }
}

// a cell's shape for this frame, worked out once and shared by every strip it crosses
struct CellPrep {
    float rad[64], rmin, rmax, inner2, far2, nx, ny, bub[3][2], r, ts;
    int   cx, cy, ext, nc, ns, cr, cg, cb;  // nc hairs of ns dots, worked out per strip (keeps this small)
    bool  nucleus;
};
CellPrep* prep;  // one per snapshot cell

// any membrane: a living, breathing outline, see-through inside, with cilia (ciliaMul 0 = none)
void prepBlob(CellPrep& p, float x, float y, float r, float heading, const float ph[3], float wob, bool bop, int cr, int cg, int cb,
              uint32_t t, float ciliaMul = 1, float ciliaLen = 6, bool nucleus = true) {
    float ts = t * 0.001f;
    p.cx = x, p.cy = y, p.r = r, p.nucleus = nucleus;
    p.rmin = 1e9, p.rmax = 0;
    for (int a = 0; a < 64; a++) {
        float th = a * 6.283f / 64;
        float w  = 0.05f * fsin(3 * th + ts * 1.3f + ph[0]) + 0.035f * fsin(5 * th - ts * 2.1f + ph[1]) +
                  0.03f * fsin(2 * th + ts * 0.7f + ph[2]) + wob * 0.08f * fsin(7 * th + ts * 20);
        p.rad[a] = r * (1 + w);
        p.rmin   = min(p.rmin, p.rad[a]);
        p.rmax   = max(p.rmax, p.rad[a]);
    }
    p.ext    = p.rmax + ciliaLen + 4;  // cilia reach past the membrane
    p.far2   = (p.rmax + 4) * (p.rmax + 4);
    float in = p.rmin - 3.3f;  // inside this, no rim: just the see-through body
    p.inner2 = in > 0 ? in * in : 0;
    p.cr = cr, p.cg = cg, p.cb = cb;
    if (bop) p.cr = 255, p.cg = 120, p.cb = 120;
    // nucleus, a little behind centre, and food bubbles circling inside
    p.nx = x - fcos(heading) * r * 0.25f, p.ny = y - fsin(heading) * r * 0.25f;
    for (int v = 0; v < 3; v++) {
        float a = ts * (0.6f + v * 0.25f) + v * 2.1f + ph[0], rr = r * 0.55f;
        p.bub[v][0] = x + fcos(a) * rr, p.bub[v][1] = y + fsin(a) * rr * 0.8f;
    }
    p.nc = ciliaMul > 0 ? min(66, (int)((18 + r * 0.8f) * ciliaMul)) : 0;
    p.ns = min(12, (int)ciliaLen);
    p.ts = ts;
}

// cilia: fine beating hairs around the edge, only the ones reaching into this strip
void drawCilia(const CellPrep& p) {
    int cr = p.cr / 6, cg = p.cg / 6, cb = p.cb / 6;
    for (int h = 0; h < p.nc; h++) {
        float th = h * 6.283f / p.nc, sn = fsin(th);
        float r0 = p.rad[((int)(th * 64 / 6.283f)) & 63];
        if (!touches(p.cy + sn * (r0 + p.ns / 2.0f), p.ns / 2.0f + 3)) continue;
        float beat = fsin(p.ts * 9 + h * 1.7f) * 0.35f;
        for (int s = 1; s <= p.ns; s++) {
            float a = th + beat * s * 0.15f * 6 / p.ns;
            add(p.cx + fcos(a) * (r0 + s), p.cy + fsin(a) * (r0 + s), cr, cg, cb);
        }
    }
}
void prepCell(const SnapCell& k, uint32_t t, CellPrep& p) {
    prepBlob(p, k.x, k.y, k.r, k.heading, k.ph, k.wob, k.bop > 0, k.cr, k.cg, k.cb, t);
}

void drawBlob(const CellPrep& p) {
    if (!touches(p.cy, p.ext)) return;
    int cr = p.cr, cg = p.cg, cb = p.cb, br = cr / 3, bg = cg / 3, bb = cb / 3;
    int y0 = max(p.cy - p.ext, Y0), y1 = min(p.cy + p.ext, Y0 + STRIP - 1);
    for (int y = y0; y <= y1; y++) {
        int   dy = y - p.cy;
        float dy2 = dy * dy;
        if (dy2 >= p.far2) continue;
        int half = fsqrt(p.far2 - dy2), xa = max(0, p.cx - half), xb = min(SW - 1, p.cx + half);
        int row = (y - Y0) * SW, lit = 50 - dy * 3 / 2;  // see-through body, lit from below-left
        for (int x = xa; x <= xb; x++) {
            int   dx = x - p.cx, i = row + x;
            float d2 = dx * dx + dy2;
            int   body = lit - dx;
            body = body < 20 ? 20 : body > 90 ? 90 : body;
            if (d2 < p.inner2) {
                R[i] += (br - R[i]) * body >> 8, G[i] += (bg - G[i]) * body >> 8, B[i] += (bb - B[i]) * body >> 8;
                continue;
            }
            float e = p.rad[angle64(dx, dy)] - fsqrt(d2);  // >0 inside
            if (e < -4) continue;
            int ar, ag, ab;
            if (e < 0) {  // halo just outside the membrane
                int g = (int)(40 * (1 + e / 4));
                ar = cr * g >> 8, ag = cg * g >> 8, ab = cb * g >> 8;
            } else {
                R[i] += (br - R[i]) * body >> 8, G[i] += (bg - G[i]) * body >> 8, B[i] += (bb - B[i]) * body >> 8;
                float rim = 1 - e / 3.2f;  // the bright membrane
                if (rim <= 0) continue;
                ar = cr * rim * 0.75f, ag = cg * rim * 0.75f, ab = cb * rim * 0.75f;
            }
            int v = R[i] + ar, w = G[i] + ag, z = B[i] + ab;
            R[i] = v > 255 ? 255 : v, G[i] = w > 255 ? 255 : w, B[i] = z > 255 ? 255 : z;
        }
    }
    if (p.nucleus) spot(p.nx, p.ny, p.r * 0.32f, 150, 110, 210, 120, 3);
    for (auto& b : p.bub) glowDot(b[0], b[1], 70, 70, 30);
    drawCilia(p);
}

// eye-spots facing where it swims; they blink, and squint when happy
void drawEyes(float cx, float cy, float r, float heading, const SnapPet& pet) {
    float ex = fcos(heading), ey = fsin(heading), px = -ey, py = ex;
    float fx = cx + ex * r * 0.45f, fy = cy + ey * r * 0.45f, sep = r * 0.28f;
    bool  shut = pet.blink < 0.12f;
    for (int s = -1; s <= 1; s += 2) {
        float x = fx + px * sep * s, y = fy + py * sep * s;
        if (shut || pet.joy > 0) {
            for (int q = -2; q <= 2; q++) mix(x + q, y, 10, 20, 15, 220), mix(x + q, y + 1, 10, 20, 15, 220);
        } else {
            spot(x, y, 1.8f, 8, 18, 14, 230, 1.6f);
            add(x - 1, y - 1.6f, 160, 160, 150);
        }
    }
}

void drawCell(const SnapCell& k, const CellPrep& p, const SnapPet* pet) {
    drawBlob(p);
    if (k.role == ROLE_CROWNED) {  // the moment it becomes yours: a ring of warm sparks
        for (int q = 0; q < 24; q++) {
            float a = q * 6.283f / 24 + k.wob * 2, rr = p.rmax + 9 + 2 * fsin(q * 1.7f + k.heading * 3);
            glowDot(k.x + fcos(a) * rr, k.y + fsin(a) * rr, 200, 170, 80);
        }
    }
    if (pet) drawEyes(k.x, k.y, k.r, k.heading, *pet);
    else if (k.role == ROLE_HUNTER) {
        float x = k.x + fcos(k.heading) * k.r * 0.5f, y = k.y + fsin(k.heading) * k.r * 0.5f;
        spot(x, y, 2.2f, 220, 40, 30, 200, 1.6f);  // one red eyespot, like Euglena
    }
}

#include "bodies.inc"

void drawTardigrade(const Snapshot& s) {
    float tx = s.tardX, ty = s.tardY, dir = s.tardDir, ts = s.tMs * 0.001f;
    if (tx < -100 || tx > SW + 100 || !touches(ty, 30)) return;
    // four plump segments, a snout, and four pairs of stubby clawed legs
    for (int k = 0; k < 4; k++) {
        float x = tx - dir * k * 14, y = ty + fsin(ts * 3 + k) * 1.2f;
        for (int leg = 0; leg < 2; leg++) {
            float sw = fsin(ts * 4 + k * 1.6f + leg * 3.14f) * 4;
            float lx = x + sw, ly = y + (leg ? 11.7f : -11.7f);
            spot(lx, ly, 3.6f, 200, 180, 150, 110, 2);
            glowDot(lx + sw * 0.5f, ly + (leg ? 3.6f : -3.6f), 60, 50, 40);
        }
    }
    for (int k = 3; k >= 0; k--) {
        float x = tx - dir * k * 14, y = ty + fsin(ts * 3 + k) * 1.2f;
        spot(x, y, 10.4f, 205, 190, 160, 120, 3.2f);
        spot(x - 2, y - 4, 5, 240, 230, 210, 50, 4);  // translucent sheen
    }
    float hx = tx + dir * 12, hy = ty;
    spot(hx, hy, 6.8f, 210, 195, 165, 130, 2.4f);
    spot(hx + dir * 2.4f, hy - 2.4f, 1.2f, 20, 20, 20, 230, 1), spot(hx + dir * 2.4f, hy + 2.4f, 1.2f, 20, 20, 20, 230, 1);
    glowDot(hx + dir * 6.8f, hy, 50, 40, 30);
}

// ---------------------------------------------------------------- the readout, drawn over the packed strip
void hudBig(int y, uint16_t* o, int x0, int ty, const char* s, int sc, uint16_t col) {
    static const uint16_t shade = rgb(0, 10, 10);
    for (int pass = 0; pass < 2; pass++) {
        int off = pass ? 0 : sc > 1 ? 2 : 1, gr = (y - ty - off) / sc;
        if (y - ty - off < 0 || gr >= 16) continue;
        for (int i = 0; s[i]; i++) {
            uint8_t bits = font16[(uint8_t)s[i] * 16 + gr];
            for (int k = 0; k < 8; k++)
                if (bits & (0x80 >> k))
                    for (int q = 0; q < sc; q++) {
                        int x = x0 + off + (i * 8 + k) * sc + q;
                        if (x >= 0 && x < SW) o[x] = pass ? col : shade;
                    }
        }
    }
}
void hudText(int y, uint16_t* o, int x0, int ty, const char* s, uint16_t col) { hudBig(y, o, x0, ty, s, 1, col); }

// pop-up lines sit in the middle; in desk mode, under the stage readout
int bannerY(const Snapshot& s) { return s.desk ? 100 : 104; }

void hudLine(const Snapshot& s, int y, uint16_t* o) {
    static const uint16_t dim = rgb(110, 200, 180), warm = rgb(230, 200, 120), bright = rgb(200, 255, 235);
    if (s.desk) {
        int n = strlen(s.bigTime);
        hudBig(y, o, (SW - n * 24) / 2, 6, s.bigTime, 3, bright);
        // the pet's stage and leaning, small and out of the way under the time
        hudText(y, o, (SW - (int)strlen(s.hud) * 8) / 2, 58, s.hud, dim);
        hudText(y, o, (SW - (int)strlen(s.leaning) * 8) / 2, 74, s.leaning, dim);
        int m = strlen(s.info);
        hudText(y, o, SW - 4 - m * 8, 220, s.info, dim);
    } else {
        hudText(y, o, 4, 2, s.hud, dim);
        hudText(y, o, 4, 18, s.leaning, dim);
    }
    for (int l = 0; l < 2; l++)
        if (s.banner[l][0]) hudText(y, o, (SW - (int)strlen(s.banner[l]) * 8) / 2, bannerY(s) + l * 16, s.banner[l], warm);
    if (!s.showBars) return;
    // health and food bars, bottom left
    if (y >= 222 && y < 226) {
        int w = 60 * s.hp01;
        for (int x = 4; x < 64; x++) o[x] = x - 4 < w ? rgb(120, 230, 160) : rgb(20, 40, 35);
    }
    if (y >= 230 && y < 234) {
        int w = 60 * s.food01;
        for (int x = 4; x < 64; x++) o[x] = x - 4 < w ? rgb(230, 190, 90) : rgb(40, 32, 15);
    }
    hudText(y, o, 70, 220, "HP", dim);
    if (s.pet.joy > 0 && !s.desk) hudText(y, o, 250, 220, ":)", warm);  // the desk readout uses that corner
}

void prepFrame(const Snapshot& s) {
    for (int i = 0; i < s.nCells; i++) {
        const SnapCell& k = s.cells[i];
        prepCell(k, s.tMs, prep[i]);
        if (k.role == ROLE_PET || k.role == ROLE_CROWNED) prepPet(k, s.pet, s.tMs);
    }
}

// one whole strip: the drop, everything in it, packed to RGB565, the readout on top
void drawStrip(const Snapshot& s, int y0, uint16_t* out) {
    Y0 = y0;
    background(s);
    for (int i = 0; i < s.nAlgae; i++) drawAlgae(s.algae[i]);
    drawTardigrade(s);
    for (int i = s.nCells - 1; i >= 0; i--)
        if (s.cells[i].role == ROLE_PET || s.cells[i].role == ROLE_CROWNED) drawPet(s.cells[i], prep[i], s.pet, s.tMs);
        else drawCell(s.cells[i], prep[i], nullptr);
    for (int i = 0; i < s.nBits; i++) glowDot(s.bits[i][0], s.bits[i][1], 90, 120, 60);
    if (s.banner[0][0]) {  // a sparkle drifting around the pop-up
        int   w  = max(strlen(s.banner[0]), strlen(s.banner[1])) * 8 + 24, h = s.banner[1][0] ? 44 : 28;
        float cy = bannerY(s) + h / 2 - 6, fade = s.bannerAge < 1 ? s.bannerAge : 1;
        for (int q = 0; q < 18; q++) {
            float a = q * 0.35f + s.bannerAge * (0.6f + (q % 3) * 0.2f);
            float x = SW / 2 + fcos(a) * w / 2, yy = cy + fsin(a * 1.3f) * h / 2;
            int   b = (110 + 90 * fsin(s.bannerAge * 6 + q)) * fade;
            glowDot(x, yy, b, b * 0.85f, b * 0.5f);
        }
    }
    for (int i = 0; i < SW * STRIP; i++) {
        uint16_t c = ((R[i] & 0xF8) << 8) | ((G[i] & 0xFC) << 3) | (B[i] >> 3);
        out[i]     = (c >> 8) | (c << 8);
    }
    for (int yy = 0; yy < STRIP; yy++) hudLine(s, y0 + yy, out + yy * SW);
}

// ---------------------------------------------------------------- the drawing tasks
// Core 0 draws the even strips and sends every strip to the panel in order;
// a helper on core 1 draws the odd ones (the pond step there is tiny).
// Each core keeps two DMA strips, so a strip is drawn while older ones go out.
uint16_t*                sbuf[2][2];  // [core][slot]
volatile bool            pauseReq, paused;
volatile uint32_t        statFrames, statUs;
uint32_t                 lastSeq = ~0u;
const Snapshot* volatile frameSnap;
volatile int             helperDone = -1, pushed = -1;  // last odd strip drawn by the helper, last strip sent
TaskHandle_t             drawHandle, helperHandle;
SemaphoreHandle_t        frameGo;  // one give per frame: the helper's start signal (notifications only mean "a strip went out")
constexpr int            NSTRIP = SH / STRIP;

void helperTask(void*) {
    useWork(1);
    for (;;) {
        xSemaphoreTake(frameGo, portMAX_DELAY);  // a new frame
        ulTaskNotifyTake(pdTRUE, 0);  // forget the last frame's "sent" signals
        const Snapshot& s = *frameSnap;
        for (int k = 1; k < NSTRIP; k += 2) {
            while (pushed < k - 2) ulTaskNotifyTake(pdTRUE, 1);  // its slot's last strip (k - 4) is out once k - 2 is sent
            drawStrip(s, k * STRIP, sbuf[1][(k >> 1) & 1]);
            helperDone = k;
            xTaskNotifyGive(drawHandle);
        }
    }
}

void drawTask(void*) {
    useWork(0);
    for (;;) {
        if (pauseReq) {
            paused = true;
            while (pauseReq) vTaskDelay(5);
            paused = false;
        }
        uint32_t        seq;
        const Snapshot* s = snapAcquire(seq);
        if (seq == lastSeq) {  // nothing new yet
            snapRelease();
            vTaskDelay(1);
            continue;
        }
        lastSeq     = seq;
        uint32_t t0 = micros();
        prepFrame(*s);
        frameSnap = s, helperDone = -1, pushed = -1;
        xSemaphoreGive(frameGo);
        tft.startWrite();
        for (int k = 0; k < NSTRIP; k++) {
            uint16_t* buf = sbuf[k & 1][(k >> 1) & 1];
            if (k & 1)
                while (helperDone < k) ulTaskNotifyTake(pdTRUE, 1);
            else
                drawStrip(*s, k * STRIP, buf);
            tft.pushImageDMA(0, k * STRIP, SW, STRIP, (const lgfx::swap565_t*)buf);
            pushed = k;
            xTaskNotifyGive(helperHandle);
        }
        tft.endWrite();
        snapRelease();
        statUs += micros() - t0;
        statFrames++;
    }
}

}  // namespace

void renderInit() {
    bool ok = true;
    for (auto& core : sbuf)
        for (auto& b : core) ok &= (b = (uint16_t*)heap_caps_malloc(SW * STRIP * 2, MALLOC_CAP_DMA)) != nullptr;
    for (auto& core : work)
        for (auto& p : core) ok &= (p = (uint8_t*)malloc(SW * STRIP)) != nullptr;
    snaps  = (Snapshot*)calloc(3, sizeof(Snapshot));
    sinTab = (float*)malloc(1025 * sizeof(float));
    ok &= snaps && sinTab;
    for (int i = 0; ok && i <= 1024; i++) sinTab[i] = sinf(i * 6.2831853f / 1024);
    vig   = (uint8_t*)malloc(LW * LH);
    atanQ = (uint8_t(*)[128])malloc(128 * 128);
    prep  = (CellPrep*)malloc(16 * sizeof(CellPrep));
    parts = (CellPrep*)malloc(MAXPARTS * sizeof(CellPrep));
    if (!ok || !vig || !atanQ || !prep || !parts) Serial.println("out of memory for the display");
    for (int y = 0; y < LH; y++)
        for (int x = 0; x < LW; x++) {
            float dx = (x + 0.5f - LW / 2.0f) / (LW / 2.0f), dy = (y + 0.5f - LH / 2.0f) / (LH / 2.0f);
            vig[y * LW + x] = (1 - 0.21f * (dx * dx + dy * dy) - 0.2f * dx * dx * dy * dy) * 255;
        }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) atanQ[y][x] = (int)(atan2f(y, x) * 64 / 6.2831853f + 0.5f);
}

void renderTaskStart() {
    disableCore0WDT();  // the drawing keeps core 0 busy
    frameGo = xSemaphoreCreateBinary();
    // the helper outranks the pond loop (priority 1) so the odd strips never wait on it
    if (xTaskCreatePinnedToCore(helperTask, "draw1", 6144, nullptr, 2, &helperHandle, 1) != pdPASS ||
        xTaskCreatePinnedToCore(drawTask, "draw0", 6144, nullptr, 3, &drawHandle, 0) != pdPASS)
        Serial.println("no memory for the display tasks");
}

void renderPause(bool on) {
    pauseReq = on;
    // a frame takes ~40 ms; the timeout only matters if the drawing task never started (out of memory at boot),
    // and then nothing is drawing anyway, so the settings can still open
    for (int i = 0; i < 500 && paused != on; i++) delay(2);
}

void renderShot() {
    renderPause(true);
    static uint8_t* line = (uint8_t*)malloc(SW * 3);
    uint32_t        seq;
    const Snapshot* s = snapAcquire(seq);
    prepFrame(*s);
    useWork(1);  // the helper's rows: it is idle while the drawing is paused
    uint16_t* out = sbuf[1][0];
    Serial.write("SHOT", 4);
    uint16_t wh[2] = {SW, SH};
    Serial.write((uint8_t*)wh, 4);
    for (int y = 0; y < SH; y += STRIP) {
        drawStrip(*s, y, out);
        for (int k = 0; k < STRIP; k++) {
            for (int x = 0; x < SW; x++) {
                uint16_t c      = out[k * SW + x];
                c               = (c >> 8) | (c << 8);
                line[x * 3]     = (c >> 8) & 0xF8;
                line[x * 3 + 1] = (c >> 3) & 0xFC;
                line[x * 3 + 2] = (c << 3) & 0xF8;
            }
            Serial.write(line, SW * 3);
        }
    }
    snapRelease();
    renderPause(false);
}

void renderStats(uint32_t& frames, uint32_t& drawUs) {
    frames = statFrames, drawUs = statUs;
    statFrames = statUs = 0;
}

}  // namespace cp
