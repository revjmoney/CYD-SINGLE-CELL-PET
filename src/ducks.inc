// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Duck Pet's pond, drawn strip by strip with render.cpp's tools (included
// inside its namespace, after bodies.inc). The whole screen is water: a soft
// top-to-bottom gradient under a drifting diagonal ripple lattice that gives
// it the isometric angle, sun glints, lily pads and a clump of reeds.

// a filled ellipse, alpha 0..256, clipped to the strip; skip0..skip1 leaves a wedge out (a lily pad's notch)
void ellipseFill(float cx, float cy, float rx, float ry, int r, int g, int b, int a, float skip0 = 0, float skip1 = 0) {
    if (!touches(cy, ry + 1)) return;
    int y0 = max((int)(cy - ry), Y0), y1 = min((int)(cy + ry), Y0 + STRIP - 1);
    for (int y = y0; y <= y1; y++) {
        float dy = (y - cy) / ry, w = 1 - dy * dy;
        if (w <= 0) continue;
        float half = rx * fsqrt(w);
        for (int x = max(0, (int)(cx - half)); x <= min(SW - 1, (int)(cx + half)); x++) {
            if (skip1 > skip0) {
                float ang = angle64((int)(x - cx), (int)((y - cy) * rx / ry)) * 6.2831853f / 64;
                if (ang >= skip0 && ang <= skip1) continue;
            }
            mix(x, y, r, g, b, a);
        }
    }
}

void duckPond(const Snapshot& s) {
    float lamp = 0.6f + 0.4f * s.light, t = s.tMs * 0.001f;
    for (int yy = 0; yy < STRIP; yy++) {
        int   y = Y0 + yy;
        float f = y / (float)SH;  // 0 top .. 1 bottom: #6fd0e8 -> #3aa6cc -> #1d6a96
        int   r, g, b;
        if (f < 0.5f) r = 111 + (58 - 111) * f * 2, g = 208 + (166 - 208) * f * 2, b = 232 + (204 - 232) * f * 2;
        else r = 58 + (29 - 58) * (f - 0.5f) * 2, g = 166 + (106 - 166) * (f - 0.5f) * 2, b = 204 + (150 - 204) * (f - 0.5f) * 2;
        r *= lamp, g *= lamp, b *= lamp;
        uint8_t *rr = R + yy * SW, *gg = G + yy * SW, *bb = B + yy * SW;
        memset(rr, r, SW), memset(gg, g, SW), memset(bb, b, SW);
        // the ripple lattice: two families of diagonal lines drifting against each other
        int off = (int)(t * 6) % 32;
        for (int x = ((2 * y + off) % 32 + 32) % 32; x < SW; x += 32) add(x, y, 22, 26, 28);
        for (int x = ((2 * (SH - y) - off) % 32 + 64) % 32; x < SW; x += 32) add(x, y, 22, 26, 28);
    }
    // sun glints
    for (int i = 0; i < 14; i++) {
        float x = fmodf(i * 71 + t * 9, 330) - 5, y = (i * 43) % 230 + 5, a = 0.25f + 0.25f * fsin(t * 2 + i);
        if (!rowIn((int)y)) continue;
        for (int k = 0; k < 6; k++) add(x + k, y, 255 * a, 255 * a, 255 * a);
    }
    // lily pads (a notch cut out) and reeds in the bottom-left corner
    static const float pads[3][3] = {{40, 200, 14}, {285, 60, 12}, {250, 210, 10}};
    for (auto& p : pads) {
        ellipseFill(p[0], p[1] + 1.5f, p[2], p[2] * 0.5f, 20, 60, 30, 90);  // a little shadow
        ellipseFill(p[0], p[1], p[2], p[2] * 0.5f, 63, 156, 74, 256, 0, 0.45f);
    }
    for (int i = 0; i < 6; i++) dotLine(6 + i * 4, SH, 10 + i * 5 + fsin(t + i) * 2, 196 - i * 6, 40, 70, 30, 1);
}


// ---------------------------------------------------------------- the ducklings
// a soft radial-gradient ellipse: c0 at the highlight, c1 by 55%, c2 at the rim; alpha 0..256
struct RGB3 {
    int r, g, b;
};
void ellipseGrad(float cx, float cy, float rx, float ry, RGB3 c0, RGB3 c1, RGB3 c2, float hx, float hy, int a) {
    if (!touches(cy, ry + 1) || a <= 0) return;
    int   y0 = max((int)(cy - ry), Y0), y1 = min((int)(cy + ry), Y0 + STRIP - 1);
    float span = 1.0f / (max(rx, ry) * 1.25f);
    for (int y = y0; y <= y1; y++) {
        float dy = (y - cy) / ry, w = 1 - dy * dy;
        if (w <= 0) continue;
        float half = rx * fsqrt(w);
        for (int x = max(0, (int)(cx - half)); x <= min(SW - 1, (int)(cx + half)); x++) {
            float d = fsqrt((x - hx) * (x - hx) + (y - hy) * (y - hy)) * span;
            if (d > 1) d = 1;
            RGB3 c;
            if (d < 0.55f) {
                float f = d / 0.55f;
                c = {(int)(c0.r + (c1.r - c0.r) * f), (int)(c0.g + (c1.g - c0.g) * f), (int)(c0.b + (c1.b - c0.b) * f)};
            } else {
                float f = (d - 0.55f) / 0.45f;
                c = {(int)(c1.r + (c2.r - c1.r) * f), (int)(c1.g + (c2.g - c1.g) * f), (int)(c1.b + (c2.b - c1.b) * f)};
            }
            // a soft antialiased rim
            float edge = (half - fabsf(x - cx)) + 0.5f;
            mix(x, y, c.r, c.g, c.b, edge < 1 ? (int)(a * edge) : a);
        }
    }
}
// small text into the strip rows (the VGA font), for a quack bubble or a "z"
void tinyText(int x0, int y0, const char* s, int r, int g, int b) {
    if (!touches(y0 + 8, 9)) return;
    for (int i = 0; s[i]; i++)
        for (int gy = 0; gy < 16; gy += 2) {
            uint8_t bits = font16[(uint8_t)s[i] * 16 + gy];
            for (int k = 0; k < 8; k += 1)
                if (bits & (0x80 >> k)) mix(x0 + i * 6 + k * 3 / 4, y0 + gy / 2, r, g, b, 230);
        }
}

// colours: yellow toy, moonlit, or evil
void duckColors(const SnapDuck& q, const evo::Traits& tr, bool pet, RGB3& c0, RGB3& c1, RGB3& c2) {
    if (q.evil) {
        c0 = {138, 122, 58}, c1 = {90, 74, 28}, c2 = {46, 36, 16};
        return;
    }
    c0 = {255, 247, 168}, c1 = {255, 216, 58}, c2 = {232, 168, 16};
    if (pet && tr.sun < 0) {  // raised in a dim room: a pale, cool moonlight yellow (never the evil one's brown)
        float m = -tr.sun;
        c0 = {(int)(255 - 15 * m), (int)(247 + 3 * m), (int)(168 + 72 * m)};
        c1 = {(int)(255 - 45 * m), (int)(216 + 9 * m), (int)(58 + 112 * m)};
        c2 = {(int)(232 - 102 * m), (int)(168 - 8 * m), (int)(16 + 134 * m)};
    }
}

void drawDuck(const SnapDuck& q, const SnapPet& p, uint32_t tMs) {
    bool  pet = q.role != DUCKLING;
    float t = tMs * 0.001f, depth = 0.8f + 0.4f * (q.y - 40) / 200.0f;
    float s = q.size / 13 * 0.95f * depth, f = q.face;  // f flips (and squashes while turning)
    int   a = (int)(256 * q.alpha);
    if (a <= 4 || !touches(q.y - 10 * s, 30 * s + 12)) return;
    const evo::Traits tr  = pet ? p.shape : evo::Traits{0, 0, 0};
    int               st  = pet ? (p.stage < 1 ? 1 : p.stage) : 1;
    bool              sleep = q.look & LOOK_SLEEP, preen = q.look & LOOK_PREEN, startled = q.look & LOOK_STARTLED;
    RGB3              c0, c1, c2;
    duckColors(q, tr, pet, c0, c1, c2);
    float plumpW = pet ? 1 + 0.15f * (tr.plump + 1) : 1;
    float headR = (st == 1 ? 10.5f : st == 2 ? 9.5f : 9) * (pet ? 1 : 1.05f), neck = st >= 3 ? 3 : 0;
    float hb = fsin(t * 3 + q.bob) * 1.2f * (sleep ? 0.3f : 1);
    auto  X = [&](float lx) { return q.x + lx * s * f; };
    auto  Y = [&](float ly) { return q.y + ly * s; };
    // the shadow on the water and a ripple ring
    ellipseFill(q.x, Y(9), 17 * s * plumpW, 6 * s, 0, 30, 50, a / 4);
    for (int k = 0; k < 36; k++) {
        float ang = k * 6.2831853f / 36, rr = 19 + fsin(t * 3 + q.bob) * 1.5f;
        float x = q.x + fcos(ang) * rr * s * plumpW, y = Y(8) + fsin(ang) * 7 * s;
        add(x, y, 30 * q.alpha, 34 * q.alpha, 36 * q.alpha);
    }
    if (q.dabble > 0) {  // tipped up, head under water: just the body and a tail in the air
        ellipseGrad(q.x, Y(-2), 13 * s * plumpW, 10 * s, c0, c1, c2, X(-4), Y(-6), a);
        ellipseGrad(X(-6), Y(-11), 5 * s, 7 * s, c0, c1, c2, X(-7), Y(-14), a);
        for (int k = 0; k < 10; k++) add(X(10 + k * 0.6f), Y(4 + fsin(t * 9 + k) * 1.5f), 70, 70, 70);  // bubbles where the bill is
        return;
    }
    // the tail flick, the body, the wing
    ellipseGrad(X(-15), Y(-6), 5 * s, 3.5f * s, c0, c1, c2, X(-15), Y(-8), a);
    ellipseGrad(q.x, q.y, 17 * s * plumpW, 11 * s, c0, c1, c2, X(-5), Y(-4), a);
    if (q.evil || !pet || tr.sun >= 0) ellipseFill(X(-5), Y(-5), 5 * s, 2.2f * s, 255, 255, 255, q.evil ? a / 4 : a * 3 / 4);  // the bath-toy shine
    if (pet && tr.sun < 0) {  // moonlit: a blue-green sheen, and sparkles when it's dark
        ellipseFill(X(-2), Y(-4), 11 * s, 4 * s, 60, 170, 180, (int)(a * 0.25f * -tr.sun));
        for (int k = 0; k < 6; k++)
            if (p.glow > 0.2f) glowDot(X(-12 + k * 5), Y(-3 + fsin(t * 2 + k) * 3), 80 * p.glow, 200 * p.glow, 255 * p.glow);
    }
    if (st >= 3) {  // wing feathers: pin feathers at 3, smooth wings after
        RGB3 w = {c2.r * 3 / 4, c2.g * 3 / 4, c2.b * 3 / 4};
        int  wa = st == 3 ? a * 2 / 3 : a / 2;
        for (int k = 0; k < (st == 3 ? 5 : 1); k++) {
            if (st == 3) dotLine(X(-8 + k * 3), Y(-2), X(-4 + k * 3), Y(4), w.r, w.g, w.b, 1);
            else ellipseFill(X(-3), Y(1), 10 * s, 4.5f * s, w.r, w.g, w.b, wa);
        }
    }
    if (q.flap > 0 || startled) ellipseGrad(X(-3), Y(-9 - q.flap * 6), 8 * s, 3 * s, c0, c1, c2, X(-3), Y(-11), a);  // a flapping wing
    // the head (tucked in when asleep, turned back when preening)
    float hx = sleep ? 2 : preen ? -6 : 9, hy = sleep ? -6 : -13 - neck + hb;
    float hr = headR * (sleep ? 0.85f : 1), bf = preen ? -1 : 1;  // bill faces back while preening
    if (st == 1 || st == 2)  // baby fuzz around the head
        for (int k = 0; k < 18; k++) {
            float ang = k * 6.2831853f / 18;
            add(X(hx + fcos(ang) * (hr + 1.2f)), Y(hy + fsin(ang) * (hr + 1.2f)), c0.r / 5, c0.g / 5, c0.b / 6);
        }
    ellipseGrad(X(hx), Y(hy), hr * s, hr * s, c0, c1, c2, X(hx - 1), Y(hy - 3), a);
    if (!q.evil && !(pet && tr.sun < 0)) ellipseFill(X(hx - 3), Y(hy - 5.5f), 2.8f * s, 1.5f * s, 255, 255, 255, a * 3 / 4);
    if (st == 2)  // a down tuft
        for (int k = -1; k <= 1; k++) dotLine(X(hx + k * 2), Y(hy - hr), X(hx + k * 3), Y(hy - hr - 4), c1.r, c1.g, c1.b / 2, 1);
    if (pet && tr.hunt > 0.2f)  // bossy: a crest
        for (int k = 0; k < 3; k++) dotLine(X(hx - 2 + k * 2), Y(hy - hr + 1), X(hx - 4 + k * 3), Y(hy - hr - 4 - k), 200, 110, 30, 1);
    if (!sleep) {
        float bl = st == 5 ? 7 : 5.5f, bx = hx + bf * (hr - 0.5f);
        ellipseFill(X(bx), Y(hy + 2), bl * s, 2.8f * s, q.evil ? 168 : 255, q.evil ? 72 : 138, q.evil ? 26 : 30, a);
        ellipseFill(X(bx), Y(hy + 3.2f), bl * 0.85f * s, 1.3f * s, q.evil ? 120 : 224, q.evil ? 50 : 106, 16, a);
        if (q.look & LOOK_QUACK) ellipseFill(X(bx + bf * 1.5f), Y(hy + 3), bl * 0.6f * s, 1.6f * s, 90, 20, 10, a);  // bill open
    }
    // the eye
    float ex = hx + bf * 3, ey = hy - 1.5f, er = 1.9f * s * (pet && tr.hunt < -0.2f ? 1.35f : 1) * (startled ? 1.3f : 1);
    bool  shut = sleep || (pet ? p.blink < 0.12f : fmodf(t + q.bob * 3, 3.7f) < 0.12f) || (pet && p.joy > 0);
    if (q.evil) {
        dotLine(X(ex - 2.5f), Y(ey - 3.5f), X(ex + 2.5f), Y(ey - 1.5f), 18, 10, 4, 0.7f);  // the scowl
        spot(X(ex), Y(ey), er * 1.1f, 255, 42, 16, 240, 1.2f);
        spot(X(ex), Y(ey), er * 2.2f, 255, 60, 20, 70, 2);
    } else if (shut) {
        for (int k = -2; k <= 2; k++) mix(X(ex + k * 0.6f), Y(ey), 27, 27, 27, a);
    } else {
        spot(X(ex), Y(ey), er, 27, 27, 27, a * 15 / 16, 0.8f);
        add(X(ex + 0.6f), Y(ey - 0.8f), 200, 200, 200);
        if (pet && tr.hunt > 0.2f) dotLine(X(ex - 2.5f), Y(ey - 3), X(ex + 2), Y(ey - 2.5f), 120, 70, 20, 0.8f);  // a determined brow
        if (pet && tr.hunt < -0.2f) spot(X(ex - 2), Y(ey + 3.5f), 1.8f * s, 255, 120, 140, a / 3, 1.5f);  // sweet: blush
    }
    if (q.look & LOOK_QUACK) tinyText(X(hx + bf * 8) - 12, Y(hy - 16) - 4, "QUACK", 255, 255, 255);
    if (sleep) tinyText(X(hx) + 4, Y(hy - 12) - fmodf(t * 4, 6), "z", 220, 230, 255);
    if (q.role == DUCK_CROWNED)  // the moment it becomes yours
        for (int k = 0; k < 24; k++) {
            float ang = k * 6.2831853f / 24 + t, rr = 26 * s + 2 * fsin(k * 1.7f + t * 3);
            glowDot(q.x + fcos(ang) * rr, Y(-4) + fsin(ang) * rr * 0.6f, 200, 170, 80);
        }
    if (pet && p.shimmer > 0)  // it just grew: sparks drifting outward
        for (int k = 0; k < 28; k++) {
            float ang = k * 6.2831853f / 28 + t, rr = s * (24 + (3 - p.shimmer) * 6);
            glowDot(q.x + fcos(ang) * rr, Y(-4) + fsin(ang) * rr * 0.6f, 160 * p.shimmer / 3, 200 * p.shimmer / 3, 255 * p.shimmer / 3);
        }
}

// ---------------------------------------------------------------- everything else in the pond
void drawPrey(const SnapPrey& c) {
    if (!touches(c.y, 14)) return;
    float ph = c.phase;
    if (c.type == 0) {  // a bug: a dark body and ticking legs
        for (int k = -1; k <= 1; k++) {
            float wig = fsin(ph * 20 + k) * 1.5f;
            dotLine(c.x - 3, c.y + k * 1.5f + wig, c.x + 3, c.y + k * 1.5f - wig, 25, 20, 10, 0.8f);
        }
        spot(c.x, c.y, 1.8f, 40, 30, 15, 230, 1);
    } else if (c.type == 1) {  // a frog: green, legs kicking
        float kick = fsin(ph * 6) * 3;
        dotLine(c.x - 4, c.y + 2, c.x - 9 - kick, c.y + 5, 60, 140, 50, 0.8f);
        dotLine(c.x + 4, c.y + 2, c.x + 9 + kick, c.y + 5, 60, 140, 50, 0.8f);
        ellipseFill(c.x, c.y, 6, 4, 70, 160, 60, 240);
        ellipseFill(c.x, c.y - 0.5f, 3, 1.5f, 140, 210, 110, 160);
        spot(c.x - 2.5f, c.y - 3, 1.4f, 30, 30, 20, 240, 0.8f), spot(c.x + 2.5f, c.y - 3, 1.4f, 30, 30, 20, 240, 0.8f);
    } else {  // a goldfish just under the surface: fades as it sinks
        int   a = (int)(240 * (1 - 0.8f * c.depth));
        float sw = fsin(ph * 8) * 2;
        ellipseFill(c.x, c.y, 7, 3, 255, 130, 30, a);
        ellipseFill(c.x - 8, c.y + sw * 0.5f, 3, 2.5f, 255, 100, 20, a * 3 / 4);
        add(c.x + 4, c.y - 1, 60 * (1 - c.depth), 60 * (1 - c.depth), 60 * (1 - c.depth));
    }
}
void drawTurtle(const Snapshot& s) {
    float x = s.turtleX, y = s.turtleY, d = s.turtleDir, t = s.tMs * 0.001f;
    if (x < -60 || x > SW + 60 || !touches(y, 16)) return;
    for (int k = 0; k < 4; k++) {  // flippers
        float sw = fsin(t * 2 + k * 1.6f) * 2;
        ellipseFill(x + (k < 2 ? -9 : 9) + sw, y + (k % 2 ? 6 : -6), 4, 2, 60, 110, 70, 230);
    }
    ellipseFill(x + d * 15, y, 4, 3.2f, 90, 140, 90, 240);  // the head
    ellipseFill(x, y, 15, 9.5f, 40, 90, 50, 245);           // the shell
    for (int k = 0; k < 5; k++) ellipseFill(x - 8 + k * 4, y + ((k & 1) ? 3 : -3), 2.6f, 2, 70, 130, 70, 200);
    ellipseFill(x - 4, y - 4, 5, 2, 120, 170, 110, 110);
}

void prepDucks(const Snapshot& s, uint8_t* order) {  // back to front
    for (int i = 0; i < s.nDucks; i++) order[i] = i;
    for (int i = 1; i < s.nDucks; i++)
        for (int j = i; j > 0 && s.ducks[order[j]].y < s.ducks[order[j - 1]].y; j--) {
            uint8_t k = order[j];
            order[j] = order[j - 1], order[j - 1] = k;
        }
}
uint8_t duckOrder[16];

// everything in the pond, for one strip
void duckScene(const Snapshot& s) {
    duckPond(s);
    for (int i = 0; i < s.nPrey; i++)
        if (s.prey[i].type == 2) drawPrey(s.prey[i]);  // goldfish swim under everything
    for (int i = 0; i < s.nFood; i++) {
        float x = s.food[i][0], y = s.food[i][1];
        if (!touches(y, 3)) continue;
        if (s.foodType[i]) spot(x, y, 1.6f, 120, 200, 70, 230, 0.8f);  // peas (and the odd oat)
        else ellipseFill(x - 1.2f, y, 2, 1.1f, 70, 160, 60, 200), ellipseFill(x + 1.2f, y + 0.6f, 2, 1.1f, 80, 170, 60, 200);  // duckweed
    }
    for (int i = 0; i < s.nPrey; i++)
        if (s.prey[i].type != 2) drawPrey(s.prey[i]);
    drawTurtle(s);
    for (int i = 0; i < s.nDucks; i++) drawDuck(s.ducks[duckOrder[i]], s.pet, s.tMs);
    for (int i = 0; i < s.nSplat; i++) {
        const SnapSplat& p = s.splat[i];
        if (!touches(p.y, 3)) continue;
        float a = p.life > 1 ? 1 : p.life;
        if (p.red) spot(p.x, p.y, 1.5f, 190, 20, 25, (int)(240 * a), 0.8f);
        else ellipseFill(p.x, p.y, 3, 1.2f, 70, 55, 25, (int)(230 * a));  // a dark feather
    }
}
