// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// WiFi only ever runs briefly at boot: sync the clock over NTP, then the
// radio is switched off again so the demos keep their RAM.
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <time.h>

void        netPortalIfRequested();  // call early in setup; runs WPS or keyboard setup if asked, never returns then
void        netSyncAtBoot();         // sets the timezone; joins known or open WiFi and syncs NTP
bool        netHasTime();            // the clock has been set since power-up
time_t      netLastSync();           // 0 = never
const char* netJoined();             // network used for the last sync, "" if none
bool        netConfigured();         // at least one network remembered
void        netAddNetwork(const String& ssid, const String& pass);
void        netForget();
void        netForgetNetwork(const String& ssid);  // just that one
void        netRequestWps();         // reboot into WPS push-button setup
void        netRequestKeyboard();    // reboot into the touch keyboard setup (pick a network, type the password)
void        netRequestSync();        // reboot, which re-syncs
int         netTzCount();
int         netTz();
void        netSetTz(int i);
const char* netTzName(int i);

// weather: fetched with the time at boot (3 days, hourly) for a US ZIP code,
// then read out hour by hour with the radio off
void        netSetZip(const String& zip);  // "" turns weather off
String      netZip();
bool        netWeather(time_t now, int& tempF, int& code, int& hiF, int& loF);
const char* netWeatherName(int code);  // short, e.g. "PT CLOUDY"
