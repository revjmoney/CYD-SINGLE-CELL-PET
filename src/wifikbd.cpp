// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// WiFi set up on the touch screen: pick a network from a scan (or type a
// hidden one's name), type the password on an on-screen keyboard, test it,
// save it. Runs at boot in place of the pet when the settings ask for
// it, so the scan has the whole heap.
#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <vector>
#include "board.h"
#include "net.h"

namespace {

int W, H;

// serial test hook: "T x y" is a tap at x,y
bool serialTap(int& x, int& y) {
    if (!Serial.available() || Serial.read() != 'T') return false;
    x = Serial.parseInt();
    y = Serial.parseInt();
    return true;
}

// a tap: where the finger came down, reported once it lifts again
void tap(int& x, int& y) {
    int tx, ty;
    while (boardTouch(tx, ty)) delay(10);
    while (!boardTouch(x, y))
        if (serialTap(x, y)) return;
        else delay(10);
    delay(25);  // let the resistive reading settle, then take a second look
    if (boardTouch(tx, ty)) x = tx, y = ty;
    while (boardTouch(tx, ty)) delay(10);
    delay(30);
}

void button(int x, int y, int w, int h, const char* s, uint16_t fg = TFT_WHITE, uint16_t bg = 0x2945) {
    tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 4, bg);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.drawString(s, x + w / 2, y + h / 2 + 1);
    tft.setTextDatum(textdatum_t::top_left);
}
bool in(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

// title bar with a CANCEL button on the right
void header(const char* title) {
    tft.fillRect(0, 0, W, 26, TFT_DARKGREEN);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawString(title, 6, 9);
    button(W - 64, 0, 64, 26, "CANCEL", TFT_WHITE, 0x8000);
}

void message(const char* l1, const char* l2 = "", uint16_t c = TFT_GREEN) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(c, TFT_BLACK);
    tft.drawString(l1, 8, 80);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(l2, 8, 110);
}

// ---------------------------------------------------------------- keyboard
// 10 keys of 32x36 per row, five rows under the text field. Three pages:
// lower case, upper case (SHIFT, stays on until tapped again), symbols.
enum : char { K_SHIFT = 1, K_DEL, K_PAGE, K_OK };
struct Key {
    int16_t x, y, w;
    char    c;
};
constexpr int KY = 60, KH = 36, KW = 32;

int layout(int page, Key* k) {
    static const char* rows[3][3] = {{"qwertyuiop", "asdfghjkl-", "zxcvbnm_"},
                                     {"QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_"},
                                     {"!\"#$%&'()*", "+-:;<=>?[\\", "]^_`{|}~"}};
    int n = 0;
    auto add = [&](int col, int row, int w, char c) { k[n++] = {(int16_t)(col * KW), (int16_t)(KY + row * KH), (int16_t)(w * KW), c}; };
    for (int i = 0; i < 10; i++) add(i, 0, 1, "1234567890"[i]);
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 10; i++) add(i, r + 1, 1, rows[page][r][i]);
    add(0, 3, 1, K_SHIFT);
    for (int i = 0; i < 8; i++) add(i + 1, 3, 1, rows[page][2][i]);
    add(9, 3, 1, K_DEL);
    add(0, 4, 2, K_PAGE);
    add(2, 4, 2, ' ');
    add(4, 4, 1, ',');
    add(5, 4, 1, '.');
    add(6, 4, 1, '/');
    add(7, 4, 1, '@');
    add(8, 4, 2, K_OK);
    return n;
}

void drawKey(const Key& k, int page) {
    char s[8] = {k.c, 0};  // room for "space"
    uint16_t bg = 0x2945;
    switch (k.c) {
        case K_SHIFT: strcpy(s, page == 2 ? "" : "^"), bg = page == 1 ? 0x03E0 : 0x4208; break;
        case K_DEL: strcpy(s, "<x"), bg = 0x4208; break;
        case K_PAGE: strcpy(s, page == 2 ? "abc" : "#+="), bg = 0x4208; break;
        case K_OK: strcpy(s, "OK"), bg = 0x03E0; break;
        case ' ': strcpy(s, "space"); break;
    }
    tft.setTextSize(k.c == ' ' || k.c == K_PAGE ? 1 : 2);
    button(k.x, k.y, k.w, KH, s, TFT_WHITE, bg);
}

void drawField(const String& t, const char* hint) {
    tft.fillRect(0, 27, W, 32, TFT_BLACK);
    tft.drawRect(2, 29, W - 4, 28, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    String s = t.length() > 23 ? "<" + t.substring(t.length() - 22) : t;
    tft.drawString(s + "_", 8, 36);
    if (!t.length() && hint) {
        tft.setTextSize(1);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString(hint, 30, 40);
    }
}

// false when cancelled; text holds what was typed (and starts with its value)
bool typeText(const char* title, String& text, int maxLen, const char* hint = nullptr) {
    Key k[48];  // layout() makes 47
    int page = 0, n = 0;
    tft.fillScreen(TFT_BLACK);
    header(title);
    for (bool redraw = true;;) {
        if (redraw) {
            n = layout(page, k);
            tft.fillRect(0, KY, W, H - KY, TFT_BLACK);
            for (int i = 0; i < n; i++) drawKey(k[i], page);
            redraw = false;
        }
        drawField(text, hint);
        Serial.printf("kbd: page %d, %d chars\n", page, (int)text.length());  // length only, never the text
        int x, y;
        tap(x, y);
        if (in(x, y, W - 64, 0, 64, 26)) return false;
        for (int i = 0; i < n; i++) {
            if (!in(x, y, k[i].x, k[i].y, k[i].w, KH)) continue;
            switch (char c = k[i].c) {
                case K_SHIFT:
                    if (page < 2) page ^= 1, redraw = true;
                    break;
                case K_PAGE: page = page == 2 ? 0 : 2, redraw = true; break;
                case K_DEL:
                    if (text.length()) text.remove(text.length() - 1);
                    break;
                case K_OK: return true;
                default:
                    if ((int)text.length() < maxLen) text += c;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------- network list
struct Net {
    String ssid;
    int    rssi;
    bool   open;
};

void scan(std::vector<Net>& nets) {
    message("SCANNING...", "looking for WiFi networks");
    nets.clear();
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {  // strongest first; one row per name
        String s = WiFi.SSID(i);
        bool   dup = !s.length();
        for (auto& e : nets) dup |= e.ssid == s;
        if (!dup) nets.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    }
    std::sort(nets.begin(), nets.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
}

// 0 cancel, 1 picked (ssid/open set), 2 type a hidden network's name
int pickNetwork(String& ssid, bool& open) {
    constexpr int ROWS = 6, RY = 28, RH = 30, BY = RY + ROWS * RH + 2;
    std::vector<Net> nets;
    scan(nets);
    int top = 0;
    for (;;) {
        tft.fillScreen(TFT_BLACK);
        header("PICK YOUR NETWORK");
        tft.setTextSize(2);
        for (int r = 0; r < ROWS; r++) {
            int i = top + r, y = RY + r * RH;
            if (i >= (int)nets.size()) break;
            tft.fillRoundRect(2, y + 1, W - 4, RH - 2, 4, 0x18E3);
            tft.setTextColor(TFT_WHITE, 0x18E3);
            tft.drawString(nets[i].ssid.substring(0, 18), 8, y + 8);
            int bars = nets[i].rssi > -55 ? 4 : nets[i].rssi > -65 ? 3 : nets[i].rssi > -75 ? 2 : 1;
            for (int b = 0; b < 4; b++)
                tft.fillRect(W - 48 + b * 6, y + 22 - b * 4, 4, 4 + b * 4, b < bars ? TFT_GREEN : TFT_DARKGREY);
            tft.setTextSize(1);
            tft.setTextColor(nets[i].open ? TFT_CYAN : TFT_LIGHTGREY, 0x18E3);
            tft.drawString(nets[i].open ? "open" : "lock", W - 22, y + 11);
            tft.setTextSize(2);
        }
        if (nets.empty()) {
            tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            tft.drawString("no networks found", 8, RY + 20);
        }
        tft.setTextSize(1);
        int bw = W / 4;
        button(0, BY, bw, H - BY, "< MORE");
        button(bw, BY, bw, H - BY, "HIDDEN...");
        button(2 * bw, BY, bw, H - BY, "RESCAN");
        button(3 * bw, BY, bw, H - BY, "MORE >");
        int x, y;
        tap(x, y);
        if (in(x, y, W - 64, 0, 64, 26)) return 0;
        if (y >= BY) {
            int b = x / bw;
            if (b == 0) top = max(0, top - ROWS);
            if (b == 1) return 2;
            if (b == 2) scan(nets), top = 0;
            if (b == 3 && top + ROWS < (int)nets.size()) top += ROWS;
            continue;
        }
        int i = top + (y - RY) / RH;
        if (y >= RY && i < (int)nets.size()) {
            ssid = nets[i].ssid;
            open = nets[i].open;
            return 1;
        }
    }
}

// an open network gets a second look: TV, printer and camera hotspots are
// open too and never reach the internet
bool confirmOpen(const String& ssid) {
    message("OPEN NETWORK (NO PASSWORD)", ssid.c_str(), TFT_CYAN);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("TV, printer and camera hotspots are open", 8, 130);
    tft.drawString("too, but have no internet.", 8, 142);
    int bw = W / 2, by = H - 50;
    button(0, by, bw, 50, "JOIN AND SAVE");
    button(bw, by, bw, 50, "BACK", TFT_WHITE, 0x8000);
    int x, y;
    do tap(x, y);
    while (y < by);
    return x < bw;
}

bool join(const String& ssid, const String& pass) {
    message("JOINING...", ssid.c_str());
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) delay(100);
    return WiFi.status() == WL_CONNECTED;
}

}  // namespace

// Never returns: restarts into the pet when done or cancelled.
void wifiKeyboardSetup() {
    W = tft.width();
    H = tft.height();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    for (;;) {
        String ssid, pass;
        bool   open = false;
        int    got  = pickNetwork(ssid, open);
        if (!got) break;
        if (got == 2 && !typeText("NETWORK NAME", ssid, 32, "the hidden network's name")) continue;
        if (got == 1 && open && !confirmOpen(ssid)) continue;
        bool typed = got == 2 || !open;
        while (!typed || typeText(("PASSWORD: " + ssid.substring(0, 20)).c_str(), pass, 63, "blank if it has none")) {
            if (join(ssid, pass)) {
                netAddNetwork(ssid, pass);
                message("WIFI SAVED", ("joined " + ssid + ", restarting...").c_str());
                delay(1500);
                ESP.restart();
            }
            // didn't join: wrong password, or out of range. Edit, keep it anyway, or back out.
            message("COULDN'T JOIN", ssid.c_str(), TFT_RED);
            int bw = W / 3, by = H - 50;
            tft.setTextSize(1);
            button(0, by, bw, 50, "EDIT PASSWORD");
            button(bw, by, bw, 50, "SAVE ANYWAY");
            button(2 * bw, by, bw, 50, "BACK", TFT_WHITE, 0x8000);
            int x, y;
            do tap(x, y);
            while (y < by);
            if (x >= 2 * bw) break;
            if (x >= bw) {
                netAddNetwork(ssid, pass);
                message("WIFI SAVED", "restarting...");
                delay(1500);
                ESP.restart();
            }
            typed = true;
        }
    }
    ESP.restart();
}

// the same keyboard for any short text (the settings use it for the ZIP code)
bool kbdType(const char* title, String& text, int maxLen, const char* hint) {
    W = tft.width();
    H = tft.height();
    return typeText(title, text, maxLen, hint);
}
