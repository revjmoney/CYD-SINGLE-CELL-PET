// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The pet's five bodies, drawn strip by strip with render.cpp's tools
// (included inside its namespace). What it has been through shows: room
// light tints it green with algae partners or gives it its own glow, a hunting
// life adds amber and stinging threads, good feeding makes it plump.
//   1 one cell   2 a fused pair   3 a ball colony   4 a dome whose cells have jobs   5 a jellyfish

CellPrep* parts;  // the pet's cells in stages 2..4
int       nParts;
constexpr int MAXPARTS = 10;

// the pet's colour: its own hue, pulled toward green (sun) or cool blue (glow)
void petTint(const SnapCell& k, const SnapPet& p, int& r, int& g, int& b) {
    r = k.cr, g = k.cg, b = k.cb;
    float s = p.shape.sun;
    if (s > 0) r += (90 - r) * s * 0.5f, g += (235 - g) * s * 0.5f, b += (130 - b) * s * 0.5f;
    else r += (100 - r) * -s * 0.5f, g += (180 - g) * -s * 0.5f, b += (255 - b) * -s * 0.5f;
}

// a stable pseudo-random number per (pet, index), for spots that must not jump around
inline float hash01(int i) {
    uint32_t h = i * 2654435761u;
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
    return (h & 0xFFFF) / 65535.0f;
}

void prepPet(const SnapCell& k, const SnapPet& p, uint32_t t) {
    nParts = 0;
    if (p.stage < 2 || p.stage > 4) return;
    int cr, cg, cb;
    petTint(k, p, cr, cg, cb);
    float fx = fcos(k.heading), fy = fsin(k.heading), sx = -fy, sy = fx;  // forward, side
    float ph[3];
    auto  part = [&](float u, float v, float r, float cil, int seed) {
        if (nParts >= MAXPARTS) return;
        for (int q = 0; q < 3; q++) ph[q] = k.ph[q] + seed * 1.7f + q;
        prepBlob(parts[nParts++], k.x + fx * u + sx * v, k.y + fy * u + sy * v, r, k.heading, ph, k.wob, k.bop > 0, cr, cg, cb, t, cil,
                 6, true);
    };
    if (p.stage == 2) {  // two cells stuck side by side, sharing a wall
        part(0, -k.r * 0.4f, k.r * 0.62f, 0.8f, 1);
        part(0, k.r * 0.4f, k.r * 0.62f, 0.8f, 2);
    } else if (p.stage == 3) {  // a ball colony, turning slowly
        int n = 6 + (int)roundf(p.shape.plump * 3);
        n     = n < 4 ? 4 : n > 9 ? 9 : n;
        part(0, 0, k.r * 0.36f, 0, 0);
        for (int i = 0; i < n - 1; i++) {
            float a = p.spin + i * 6.283f / (n - 1);
            part(fcos(a) * k.r * 0.58f, fsin(a) * k.r * 0.58f, k.r * 0.34f, 0.35f, i + 3);
        }
    } else {  // a shallow dome: an arc of cells facing forward, a couple underneath
        int n = 7;
        for (int i = 0; i < n; i++) {
            float a = (i - (n - 1) / 2.0f) * 0.42f;
            part(fcos(a) * k.r * 0.55f - k.r * 0.15f, fsin(a) * k.r * 0.8f, k.r * 0.3f, 0, i + 1);
        }
        part(-k.r * 0.05f, -k.r * 0.22f, k.r * 0.26f, 0, 11);
        part(-k.r * 0.05f, k.r * 0.22f, k.r * 0.26f, 0, 12);
    }
}

// a soft line of dots, for cilia rings, threads and arms
void dotLine(float x0, float y0, float x1, float y1, int r, int g, int b, float step = 1.5f) {
    float dx = x1 - x0, dy = y1 - y0, len = fsqrt(dx * dx + dy * dy);
    int   n  = len / step + 1;
    if (!touches((y0 + y1) / 2, fabsf(dy) / 2 + 3)) return;
    for (int i = 0; i <= n; i++) {
        float f = i / (float)n;
        add(x0 + dx * f, y0 + dy * f, r, g, b);
    }
}

void drawJelly(const SnapCell& k, const SnapPet& p, uint32_t t);

void drawPet(const SnapCell& k, const CellPrep& own, const SnapPet& p, uint32_t t) {
    float ts = t * 0.001f;
    if (p.stage >= 5) drawJelly(k, p, t);
    else if (p.stage <= 1) drawCell(k, own, &p);
    else {
        int cr, cg, cb;
        petTint(k, p, cr, cg, cb);
        float fx = fcos(k.heading), fy = fsin(k.heading), sx = -fy, sy = fx;
        if (p.stage == 3) spot(k.x, k.y, k.r * 1.02f, cr / 3, cg / 3, cb / 3, 26, 8);  // the colony's shared jelly
        if (p.stage == 4) {
            // the mouth underneath, and a ring of long beating hairs along the back rim
            spot(k.x - fx * k.r * 0.35f, k.y - fy * k.r * 0.35f, k.r * 0.22f, 20, 25, 30, 150, 4);
            for (int h = -9; h <= 9; h++) {
                float v = h * k.r * 0.085f, beat = fsin(ts * 7 + h * 0.8f) * 0.5f;
                float x0 = k.x - fx * k.r * 0.25f + sx * v, y0 = k.y - fy * k.r * 0.25f + sy * v;
                dotLine(x0, y0, x0 - fx * (12 + beat * 4) + sx * beat * 6, y0 - fy * (12 + beat * 4) + sy * beat * 6, cr / 5, cg / 5,
                        cb / 5);
            }
            // a hunter grows a few short stinging threads
            int nst = p.shape.hunt > 0 ? 2 + (int)(p.shape.hunt * 2.5f) : 0;
            for (int i = 0; i < TENT && i < 4; i++) nst += p.sting[i] && i < 2;
            for (int i = 0; i < nst && i < 6; i++) {
                float v = (i - (nst - 1) / 2.0f) * k.r * 0.25f, sw = fsin(ts * 2 + i) * 5;
                float x0 = k.x - fx * k.r * 0.3f + sx * v, y0 = k.y - fy * k.r * 0.3f + sy * v;
                float x1 = x0 - fx * k.r * 0.9f + sx * sw, y1 = y0 - fy * k.r * 0.9f + sy * sw;
                dotLine(x0, y0, x1, y1, 110, 70, 30, 2);
                glowDot(x1, y1, 120, 60, 20);
            }
        }
        for (int i = 0; i < nParts; i++) drawBlob(parts[i]);
        drawEyes(k.x + fx * k.r * (p.stage == 4 ? 0.35f : 0.15f), k.y + fy * k.r * (p.stage == 4 ? 0.35f : 0.15f), k.r * 0.7f,
                 k.heading, p);
    }
    if (p.shimmer > 0) {  // it just changed: a ring of sparks drifting outward
        float rr = k.r * (1.3f + (3 - p.shimmer) * 0.25f);
        for (int q = 0; q < 28; q++) {
            float a = q * 6.283f / 28 + ts;
            glowDot(k.x + fcos(a) * rr, k.y + fsin(a) * rr, 160 * p.shimmer / 3, 200 * p.shimmer / 3, 255 * p.shimmer / 3);
        }
    }
}

// ---------------------------------------------------------------- the jellyfish
void drawJelly(const SnapCell& k, const SnapPet& p, uint32_t t) {
    float ts = t * 0.001f;
    int   cr, cg, cb;
    petTint(k, p, cr, cg, cb);
    float hunt = p.shape.hunt, sun = p.shape.sun;
    int   ar = cr, ag = cg, ab = cb;  // accents: amber for a hunter
    if (hunt > 0) ar += (255 - ar) * hunt * 0.6f, ag += (150 - ag) * hunt * 0.6f, ab += (60 - ab) * hunt * 0.6f;
    float fx = fcos(k.heading), fy = fsin(k.heading), sx = -fy, sy = fx;

    // tentacles first, so the bell sits over their roots
    for (int i = 0; i < p.nTent; i++) {
        bool stinger = hunt > 0.2f || p.sting[i];
        for (int s = 1; s < TSEG; s++) {
            float x0 = p.tent[i][s - 1][0], y0 = p.tent[i][s - 1][1], x1 = p.tent[i][s][0], y1 = p.tent[i][s][1];
            int   fade = 255 - s * 22;
            dotLine(x0, y0, x1, y1, (cr / 4) * fade >> 8, (cg / 4) * fade >> 8, (cb / 4) * fade >> 8, 1.2f);
            if (stinger && (s & 1)) glowDot(x1, y1, (ar / 3) * fade >> 8, (ag / 4) * fade >> 8, (ab / 5) * fade >> 8);
        }
    }
    // four frilly oral arms trailing from the middle (longer on a grazer)
    float armLen = k.r * (1.0f + 0.6f * max(0.0f, -hunt));
    for (int a = 0; a < 4; a++) {
        float v0 = (a - 1.5f) * k.r * 0.16f, px = k.x - fx * k.r * 0.05f + sx * v0, py = k.y - fy * k.r * 0.05f + sy * v0;
        for (int s = 1; s <= 10; s++) {
            float f = s / 10.0f, wig = fsin(ts * 2.2f + s * 0.9f + a * 1.3f) * (3 + 6 * f);
            float qx = k.x - fx * armLen * f + sx * (v0 * (1 - f * 0.4f) + wig), qy = k.y - fy * armLen * f + sy * (v0 * (1 - f * 0.4f) + wig);
            dotLine(px, py, qx, qy, cr / 5, cg / 5, cb / 5, 1.5f);
            spot(qx, qy, 1.6f + (1 - f) * 1.5f, cr / 2, cg / 2, cb / 2, 40, 2);  // the frills
            px = qx, py = qy;
        }
    }

    // the bell: a translucent dome facing where it swims; the pulse squeezes it narrow
    float aspect = 1.0f + 0.25f * (p.shape.plump + 1);  // sleek 1.0 .. plump 1.5
    float A = k.r * 0.95f * (1 + 0.12f * p.pulse), V = k.r * aspect * 0.75f * (1 - 0.2f * p.pulse);
    int   ext = max(A, V) + 4;
    if (touches(k.y, ext)) {
        int y0 = max(k.y - ext, Y0), y1 = min(k.y + ext, Y0 + STRIP - 1);
        for (int y = y0; y <= y1; y++) {
            int row = (y - Y0) * SW;
            for (int x = max(0, k.x - ext); x <= min(SW - 1, k.x + ext); x++) {
                float dx = x - k.x, dy = y - k.y, u = dx * fx + dy * fy, v = dx * sx + dy * sy;
                if (u < -2) continue;
                float q = (u / A) * (u / A) + (v / V) * (v / V);
                if (q > 1.15f) continue;
                int i = row + x;
                if (q > 1) {  // a soft halo around the dome
                    int g = (1.15f - q) * 200;
                    R[i] = min(255, R[i] + (cr * g >> 9)), G[i] = min(255, G[i] + (cg * g >> 9)), B[i] = min(255, B[i] + (cb * g >> 9));
                    continue;
                }
                float e = 1 - fsqrt(q);  // 0 at the rim, 1 at the middle
                int   body = 45 + (int)(70 * u / A);  // thicker toward the top of the dome
                R[i] += (cr / 3 - R[i]) * body >> 8, G[i] += (cg / 3 - G[i]) * body >> 8, B[i] += (cb / 3 - B[i]) * body >> 8;
                // four radial canals, faintly brighter
                int   ang = angle64((int)u, (int)v) & 15;
                float canal = ang < 2 || ang > 14 ? 0.35f : 0;
                float rim = e < 0.12f ? (0.12f - e) / 0.12f : 0;  // the bright edge
                float lit = rim * 0.8f + canal * (0.3f + 0.7f * e);
                if (u < 3) lit += 0.25f;  // the margin where the bell opens
                if (lit > 0) {
                    R[i] = min(255, R[i] + (int)(cr * lit * 0.6f)), G[i] = min(255, G[i] + (int)(cg * lit * 0.6f));
                    B[i] = min(255, B[i] + (int)(cb * lit * 0.6f));
                }
            }
        }
    }
    // little lappets along the margin
    for (int l = -6; l <= 6; l++) {
        float v = l * V / 6.5f;
        spot(k.x + sx * v - fx * 1.5f, k.y + sy * v - fy * 1.5f, 1.8f, ar / 2, ag / 2, ab / 2, 70, 1.6f);
    }
    // algae partners (sun) or its own glow spots (dark)
    if (sun > 0.15f) {
        int n = 4 + sun * 14;
        for (int i = 0; i < n; i++) {
            float u = A * (0.2f + 0.6f * hash01(i * 3)), v = V * (hash01(i * 3 + 1) * 1.4f - 0.7f) * (1 - u / A);
            glowDot(k.x + fx * u + sx * v, k.y + fy * u + sy * v, 30, 110, 30);
        }
    } else if (sun < -0.15f) {
        int   n   = 6 + -sun * 8;
        float lum = 0.35f + 0.65f * p.glow;
        for (int i = 0; i < n; i++) {
            float a = (i + 0.5f) / n * 3.14159f - 1.5708f, u = A * 0.85f * fcos(a), v = V * 0.85f * fsin(a);
            float tw = 0.7f + 0.3f * fsin(ts * 1.3f + i * 2.1f);
            glowDot(k.x + fx * u + sx * v, k.y + fy * u + sy * v, 40 * lum * tw, 140 * lum * tw, 255 * lum * tw);
        }
    }
    drawEyes(k.x + fx * A * 0.45f, k.y + fy * A * 0.45f, k.r * 0.5f, k.heading, p);
}
