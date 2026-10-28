// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "save.h"
#include <Arduino.h>
#include <Preferences.h>

namespace cp {

evo::Save petLoad(const char* ns) {
    evo::Save   s;
    Preferences p;
    p.begin(ns, true);
    size_t n = p.getBytesLength("s"), got = 0;
    bool   migrated = false;
    if (n == sizeof s) got = p.getBytes("s", &s, sizeof s);
    else if (n == sizeof(evo::SaveV1)) {  // the first layout: bring it forward
        evo::SaveV1* v1 = (evo::SaveV1*)malloc(sizeof(evo::SaveV1));  // once, at boot
        if (v1) migrated = evo::migrateV1(v1, p.getBytes("s", v1, sizeof *v1), s), free(v1);
    }
    p.end();
    p.begin("pet", true);
    bool legacy = !strcmp(ns, "pet2") && p.isKey("lvl");  // only Cell Pet ever had an older pet
    switch (evo::planLoad(n, n == sizeof s && evo::valid(s, got), migrated, legacy)) {
        case evo::LoadFrom::Pet2: break;
        case evo::LoadFrom::Migrated: Serial.println("pet: save brought up to date"); break;
        case evo::LoadFrom::Legacy:
            s = evo::upgradeOld(p.getUChar("lvl", 1), p.getFloat("xp", 0), p.getUInt("kills", 0), p.getFloat("en", 70),
                                p.getLong64("born", 0));
            Serial.println("pet: brought the skeleton pet across");
            break;
        case evo::LoadFrom::Fresh:
            if (n) Serial.println("pet: save unreadable, starting a new drop");
            s = evo::freshSave(esp_random());
            break;
    }
    p.end();
    return s;
}

void petStore(evo::Save& s, const char* ns) {
    evo::seal(s);
    Preferences p;
    p.begin(ns);
    p.putBytes("s", &s, sizeof s);
    p.end();
}

}  // namespace cp
