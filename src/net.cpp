// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net.h"
#include <Arduino.h>
#include <algorithm>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wps.h>
#include <WiFiClient.h>
#include "board.h"

static const char* const tzNames[] = {"EASTERN", "CENTRAL", "MOUNTAIN", "ARIZONA", "PACIFIC",
                                      "ALASKA",  "HAWAII",  "UTC",      "UK",      "C. EUROPE"};
static const char* const tzRules[] = {"EST5EDT,M3.2.0,M11.1.0", "CST6CDT,M3.2.0,M11.1.0", "MST7MDT,M3.2.0,M11.1.0",
                                      "MST7",                   "PST8PDT,M3.2.0,M11.1.0", "AKST9AKDT,M3.2.0,M11.1.0",
                                      "HST10",                  "UTC0",                   "GMT0BST,M3.5.0/1,M10.5.0",
                                      "CET-1CEST,M3.5.0,M10.5.0/3"};
static constexpr int NTZ = sizeof(tzNames) / sizeof(tzNames[0]);
static constexpr int MAXNET = 5;  // remembered networks, newest in slot 0

// survives a software reset (the nightly resync reboot), not a power cycle
RTC_DATA_ATTR static time_t lastSync = 0;
RTC_DATA_ATTR static char   joined[33] = "";

static void applyTz(int i) {
    setenv("TZ", tzRules[i], 1);
    tzset();
}

int         netTzCount() { return NTZ; }
const char* netTzName(int i) { return tzNames[(i % NTZ + NTZ) % NTZ]; }
int         netTz() {
    Preferences p;
    p.begin("net", true);
    int i = p.getUChar("tz", 0);  // Eastern by default
    p.end();
    return i < NTZ ? i : 0;
}
void netSetTz(int i) {
    i = (i % NTZ + NTZ) % NTZ;
    Preferences p;
    p.begin("net");
    p.putUChar("tz", i);
    p.end();
    applyTz(i);
}
bool netConfigured() {
    Preferences p;
    p.begin("net", true);
    bool ok = p.getString("s0", "").length() > 0;
    p.end();
    return ok;
}
bool        netHasTime() { return time(nullptr) > 1700000000; }
time_t      netLastSync() { return lastSync; }
const char* netJoined() { return joined; }

// remember a network (most recent first); an existing entry moves to the top
void netAddNetwork(const String& ssid, const String& pass) {
    Preferences p;
    p.begin("net");
    String s[MAXNET], k[MAXNET];
    int    n = 0;
    s[n]     = ssid;
    k[n++]   = pass;
    for (int i = 0; i < MAXNET && n < MAXNET; i++) {
        String si = p.getString(("s" + String(i)).c_str(), "");
        if (si.length() && si != ssid) {
            s[n]   = si;
            k[n++] = p.getString(("p" + String(i)).c_str(), "");
        }
    }
    for (int i = 0; i < MAXNET; i++) {
        p.putString(("s" + String(i)).c_str(), i < n ? s[i] : "");
        p.putString(("p" + String(i)).c_str(), i < n ? k[i] : "");
    }
    p.end();
}
void netForgetNetwork(const String& ssid) {
    Preferences p;
    p.begin("net");
    String s[MAXNET], k[MAXNET];
    int    n = 0;
    for (int i = 0; i < MAXNET; i++) {
        String si = p.getString(("s" + String(i)).c_str(), "");
        if (si.length() && si != ssid) {
            s[n]   = si;
            k[n++] = p.getString(("p" + String(i)).c_str(), "");
        }
    }
    for (int i = 0; i < MAXNET; i++) {
        p.putString(("s" + String(i)).c_str(), i < n ? s[i] : "");
        p.putString(("p" + String(i)).c_str(), i < n ? k[i] : "");
    }
    p.end();
}
void netForget() {
    Preferences p;
    p.begin("net");
    for (int i = 0; i < MAXNET; i++) {
        p.remove(("s" + String(i)).c_str());
        p.remove(("p" + String(i)).c_str());
    }
    p.end();
}

void netRequestWps() {
    Preferences p;
    p.begin("net");
    p.putBool("wps", true);
    p.end();
    delay(50);
    ESP.restart();
}
void netRequestKeyboard() {
    Preferences p;
    p.begin("net");
    p.putBool("kbd", true);
    p.end();
    delay(50);
    ESP.restart();
}
void netRequestSync() { ESP.restart(); }

static void status(const char* l1, const char* l2 = "", const char* l3 = "") {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(l1, 8, 90);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(l2, 8, 110);
    tft.drawString(l3, 8, 126);
}

// open networks that belong to a gadget (TV cast setup, printers, cameras):
// joinable, but never on the internet
static bool deviceHotspot(String s) {
    s.toLowerCase();
    for (const char* k : {"cast", "direct-", "setup", "vizio", "roku", "printer", "hp-", "epson", "canon", "camera", "_nomap"})
        if (s.indexOf(k) >= 0) return true;
    return false;
}

static bool ntp(int waitMs) {
    configTzTime(tzRules[netTz()], "pool.ntp.org", "time.google.com", "time.nist.gov");
    for (int i = 0; i < waitMs / 100 && !netHasTime(); i++) delay(100);
    if (!netHasTime()) {  // second go: Google's time server by IP, so slow DNS can't stall it
        Serial.println("net: no NTP answer yet, retrying by IP");
        configTzTime(tzRules[netTz()], "216.239.35.0", "216.239.35.4", "pool.ntp.org");
        for (int i = 0; i < waitMs / 100 && !netHasTime(); i++) delay(100);
    }
    return netHasTime();
}

// join scan entry `idx`: giving the exact channel and access point (BSSID)
// from the scan is far more reliable than a plain begin() right after scanning
static bool connect(int idx, const String& ssid, const String& pass) {
    const char* pw = pass.length() ? pass.c_str() : nullptr;
    for (int attempt = 0; attempt < 3; attempt++) {
        WiFi.disconnect();
        delay(100);
        if (attempt < 2 && idx >= 0) WiFi.begin(ssid.c_str(), pw, WiFi.channel(idx), WiFi.BSSID(idx));
        else WiFi.begin(ssid.c_str(), pw);  // last try: let the driver pick
        for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) delay(100);
        if (WiFi.status() == WL_CONNECTED) return true;
        Serial.printf("net: join %s try %d failed (status %d)\n", ssid.c_str(), attempt + 1, WiFi.status());
    }
    return false;
}

// ---------------------------------------------------------------- weather
// Open-Meteo (free, no key) by latitude/longitude; the ZIP is looked up once
// on zippopotam.us and the coordinates kept in NVS. Plain HTTP: no TLS stack.
static constexpr int WXH = 72;
static time_t        wxStart = 0;  // unix time of the first hour
static int8_t        wxTemp[WXH];
static uint8_t       wxCode[WXH];
static int           wxN = 0;

void netSetZip(const String& zip) {
    Preferences p;
    p.begin("net");
    p.putString("zip", zip);
    p.remove("lat");  // looked up again on the next boot
    p.remove("lon");
    p.end();
}
String netZip() {
    Preferences p;
    p.begin("net", true);
    String z = p.getString("zip", "");
    p.end();
    return z;
}
// a bare HTTP/1.0 GET (HTTPClient would pull in the whole TLS stack: 170 KB of flash)
static bool httpGet(const String& url, String& body, String* headers = nullptr) {
    if (!url.startsWith("http://")) return false;
    int         slash = url.indexOf('/', 7);
    String      host = url.substring(7, slash), path = url.substring(slash);
    WiFiClient  c;
    if (!c.connect(host.c_str(), 80, 6000)) return false;
    c.print("GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
    body = "";
    uint32_t t0 = millis();
    while ((c.connected() || c.available()) && millis() - t0 < 8000) {
        if (c.available()) body += (char)c.read();
        else delay(5);
    }
    c.stop();
    bool ok = body.startsWith("HTTP/1.") && body.substring(9, 12) == "200";
    int  hdr = body.indexOf("\r\n\r\n");
    if (headers) *headers = hdr >= 0 ? body.substring(0, hdr) : "";
    body = hdr >= 0 ? body.substring(hdr + 4) : "";
    return ok;
}

// When NTP doesn't answer (UDP gets lost now and then), any web server's
// "Date: Tue, 27 Oct 2026 16:20:00 GMT" header is good to the second.
static bool httpTime() {
    String body, h;
    httpGet("http://api.open-meteo.com/", body, &h);  // any answer will do, even an error page
    int i = h.indexOf("\r\nDate: ");
    if (i < 0) return false;
    int  d, y, hh, mm, ss;
    char mon[4] = {};
    if (sscanf(h.c_str() + i + 8, "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return false;
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char*        m      = strstr(months, mon);
    if (!m || y < 2024) return false;
    int mo = (m - months) / 3 + 1;
    // days since 1970-01-01 (civil calendar, no timegm() here)
    int  yy = y - (mo <= 2), era = yy / 400, yoe = yy - era * 400;
    int  doy  = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long days = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    timeval tv = {(time_t)(days * 86400 + hh * 3600 + mm * 60 + ss), 0};
    settimeofday(&tv, nullptr);
    Serial.println("net: NTP silent, took the time from an HTTP Date header");
    return netHasTime();
}
// the number after "key": in a JSON string, quoted or not
static bool jsonNum(const String& s, const char* key, float& v) {
    int i = s.indexOf(key);
    if (i < 0) return false;
    i += strlen(key);
    while (i < (int)s.length() && (s[i] == ':' || s[i] == ' ' || s[i] == '"')) i++;
    v = atof(s.c_str() + i);
    return true;
}
// "key":[n,n,n...] into out, returns how many
template <typename T>
static int jsonArray(const String& s, const char* key, T* out, int max) {
    int i = s.indexOf(key);
    if (i < 0) return 0;
    const char* p = s.c_str() + i + strlen(key);
    int         n = 0;
    while (*p && *p != ']' && n < max) {
        char* e;
        double v = strtod(p, &e);  // double: unix times don't fit a float
        if (e == p) break;
        out[n++] = (T)llround(v);
        p        = e;
        if (*p == ',') p++;
    }
    return n;
}
static void fetchWeather() {
    String zip = netZip();
    if (!zip.length()) return;
    Preferences p;
    p.begin("net");
    float  lat = p.getFloat("lat", NAN), lon = p.getFloat("lon", NAN);
    String body;
    if (isnan(lat) && httpGet("http://api.zippopotam.us/us/" + zip, body) && jsonNum(body, "\"latitude\"", lat) &&
        jsonNum(body, "\"longitude\"", lon)) {
        p.putFloat("lat", lat);
        p.putFloat("lon", lon);
    }
    p.end();
    if (isnan(lat)) {
        Serial.println("net: weather: ZIP lookup failed");
        return;
    }
    char url[200];
    snprintf(url, sizeof url,
             "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&hourly=temperature_2m,weather_code"
             "&temperature_unit=fahrenheit&timeformat=unixtime&forecast_days=3",
             lat, lon);
    if (!httpGet(url, body)) {
        Serial.println("net: weather: forecast failed");
        return;
    }
    long t0[1];
    int  n  = jsonArray(body, "\"time\":[", t0, 1);
    int  nt = jsonArray(body, "\"temperature_2m\":[", wxTemp, WXH);
    int  nc = jsonArray(body, "\"weather_code\":[", wxCode, WXH);
    if (n && nt && nc) {
        wxStart = t0[0];
        wxN     = min(nt, nc);
    }
    Serial.printf("net: weather %d hours, now %dF code %d\n", wxN, wxN ? wxTemp[0] : 0, wxN ? wxCode[0] : 0);
}

bool netWeather(time_t now, int& tempF, int& code, int& hiF, int& loF) {
    int i = (int)((now - wxStart) / 3600);
    if (!wxN || now < wxStart || i >= wxN) return false;
    tempF = wxTemp[i];
    code  = wxCode[i];
    // today's high and low (local calendar day)
    tm today, h;
    localtime_r(&now, &today);
    hiF = -999, loF = 999;
    for (int k = 0; k < wxN; k++) {
        time_t t = wxStart + k * 3600;
        localtime_r(&t, &h);
        if (h.tm_yday != today.tm_yday) continue;
        hiF = max(hiF, (int)wxTemp[k]);
        loF = min(loF, (int)wxTemp[k]);
    }
    return true;
}
const char* netWeatherName(int c) {  // WMO weather codes
    if (c == 0) return "CLEAR";
    if (c == 1) return "FAIR";
    if (c == 2) return "PT CLOUDY";
    if (c == 3) return "CLOUDY";
    if (c == 45 || c == 48) return "FOG";
    if (c >= 51 && c <= 57) return "DRIZZLE";
    if (c >= 61 && c <= 67) return "RAIN";
    if (c >= 71 && c <= 77) return "SNOW";
    if (c >= 80 && c <= 82) return "SHOWERS";
    if (c == 85 || c == 86) return "SNOW SHWR";
    if (c >= 95) return "T-STORMS";
    return "";
}

// is any remembered network in the last scan?
static bool savedInScan(int n) {
    Preferences q;
    q.begin("net", true);
    bool hit = false;
    for (int k = 0; k < MAXNET && !hit; k++) {
        String s = q.getString(("s" + String(k)).c_str(), "");
        for (int i = 0; s.length() && i < n && !hit; i++) hit = WiFi.SSID(i) == s;
    }
    q.end();
    return hit;
}

// Boot: join the best remembered network in range, else the strongest open
// one, set the clock over NTP, then switch the radio off again.
void netSyncAtBoot() {
    applyTz(netTz());
    status("Setting the clock...", "looking for WiFi");
    Serial.printf("net: heap %u (max block %u) before WiFi\n", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    int saved = 0;
    {
        Preferences q;
        q.begin("net", true);
        for (int k = 0; k < MAXNET; k++) saved += q.getString(("s" + String(k)).c_str(), "").length() > 0;
        q.end();
    }
    // a scan sometimes misses a network that is right there: look again before giving up on it
    for (int again = 0; again < 2 && saved && !savedInScan(n); again++) {
        Serial.printf("net: scan found %d networks, none saved; scanning again\n", n);
        n = WiFi.scanNetworks();
    }
    Serial.printf("net: scan found %d networks, %d saved\n", n, saved);
    // saved networks in range, strongest first, but gadget hotspots (a TV's
    // cast network has no internet) only after the rest
    struct Cand {
        int    idx;
        String ssid, pass;
    } cand[MAXNET];
    int nc = 0;
    Preferences p;
    p.begin("net", true);
    for (int k = 0; k < MAXNET; k++) {
        String s = p.getString(("s" + String(k)).c_str(), "");
        int    bi = -1;
        for (int i = 0; s.length() && i < n; i++)
            if (WiFi.SSID(i) == s && (bi < 0 || WiFi.RSSI(i) > WiFi.RSSI(bi))) bi = i;
        if (bi >= 0) cand[nc++] = {bi, s, p.getString(("p" + String(k)).c_str(), "")};
    }
    p.end();
    auto rank = [&](const Cand& c) { return WiFi.RSSI(c.idx) - (deviceHotspot(c.ssid) ? 1000 : 0); };
    std::sort(cand, cand + nc, [&](const Cand& a, const Cand& b) { return rank(a) > rank(b); });
    String ssid;
    bool   ok = false;
    int    slow = -1;  // first one that joined but got no time through
    for (int c = 0; c < nc && !ok; c++) {
        ssid = cand[c].ssid;
        Serial.printf("net: joining %s (signal %d dBm, channel %d)\n", ssid.c_str(), WiFi.RSSI(cand[c].idx), WiFi.channel(cand[c].idx));
        bool on = connect(cand[c].idx, ssid, cand[c].pass);
        ok      = on && (ntp(8000) || httpTime());
        if (on && !ok && slow < 0) slow = c;
        if (!ok) WiFi.disconnect();
    }
    if (!ok && slow >= 0) {  // joined but nothing got through: rejoin once and try again
        ssid = cand[slow].ssid;
        Serial.printf("net: joined %s but no time; rejoining\n", ssid.c_str());
        status("Setting the clock...", "WiFi is slow, trying again");
        ok = connect(cand[slow].idx, ssid, cand[slow].pass) && (ntp(6000) || httpTime());
    }
    // nothing we know (or it had no internet): try open networks, strongest
    // first; plenty of them (TV cast hotspots...) never reach the internet
    bool tried[64] = {};
    for (int attempt = 0; !ok && attempt < 3; attempt++) {
        int pick = -1;
        for (int i = 0; i < n && i < 64; i++)
            if (!tried[i] && WiFi.encryptionType(i) == WIFI_AUTH_OPEN && WiFi.SSID(i).length() && !deviceHotspot(WiFi.SSID(i)) &&
                (pick < 0 || WiFi.RSSI(i) > WiFi.RSSI(pick)))
                pick = i;
        if (pick < 0) break;
        tried[pick] = true;
        ssid        = WiFi.SSID(pick);
        status("Setting the clock...", ("open WiFi: " + ssid).c_str());
        ok = connect(pick, ssid, "") && ntp(4000);
        if (!ok) WiFi.disconnect();
    }
    if (ok) {
        strncpy(joined, ssid.c_str(), sizeof(joined) - 1);
        lastSync = time(nullptr);
        status("Setting the clock...", "fetching the weather");
        fetchWeather();
    }
    Serial.printf("net: %s, time %s\n", ok ? ssid.c_str() : "no wifi", netHasTime() ? "ok" : "not set");
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // radio off: the pet needs the RAM
}

// ---------------------------------------------------------------- WPS join
static volatile int wpsState = 0;  // 0 waiting, 1 got credentials, 2 failed
static void         onWifi(arduino_event_id_t e, arduino_event_info_t) {
    if (e == ARDUINO_EVENT_WPS_ER_SUCCESS) wpsState = 1;
    if (e == ARDUINO_EVENT_WPS_ER_FAILED || e == ARDUINO_EVENT_WPS_ER_TIMEOUT) wpsState = 2;
}

void wifiKeyboardSetup();  // wifikbd.cpp

void netPortalIfRequested() {
    Preferences p;
    p.begin("net");
    bool want = p.getBool("wps", false), kbd = p.getBool("kbd", false);
    if (want) p.putBool("wps", false);  // one shot: a reset leaves setup mode
    if (kbd) p.putBool("kbd", false);
    p.end();
    if (kbd) wifiKeyboardSetup();
    if (!want) return;

    status("WIFI SETUP: PRESS THE WPS BUTTON", "on your router now (2 minutes).", "Tap the screen to cancel.");
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(onWifi);
    esp_wps_config_t cfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    esp_wifi_wps_enable(&cfg);
    esp_wifi_wps_start(0);
    uint32_t t0 = millis();
    while (millis() - t0 < 120000 && wpsState == 0) {
        int x, y;
        if (boardTouch(x, y)) break;
        delay(50);
    }
    esp_wifi_wps_disable();
    if (wpsState == 1) {
        WiFi.begin();  // join with what the router handed over
        for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++) delay(100);
        if (WiFi.status() == WL_CONNECTED) {
            netAddNetwork(WiFi.SSID(), WiFi.psk());
            status("WIFI SAVED", ("joined " + WiFi.SSID()).c_str(), "restarting...");
            delay(1500);
        }
    } else {
        status("WPS DIDN'T CONNECT", "Try again, or use TYPE PASSWORD", "in the settings (hold the screen 2 s).");
        delay(4000);
    }
    ESP.restart();
}
