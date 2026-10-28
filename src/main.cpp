// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Cell Pet for the ESP32-2432S028R ("Cheap Yellow Display"): a living cell in
// a drop of pond water that you raise, and a desk clock you can leave on.
// The pet itself is in petmain.cpp and the files around it.
#include <Arduino.h>
#include "board.h"
#include "net.h"
#include "petmain.h"

void setup() {
    Serial.begin(921600);
    // 55 MHz asked (the library runs it at 40): 80 is past the panel's rating and makes the picture twitch
    boardInit(1, 55000000);
    netPortalIfRequested();  // WiFi setup, if the settings asked for it
    cp::petMain();
}

void loop() {}
