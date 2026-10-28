// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The pet's save in NVS "pet2". The skeleton's "pet" keys are only ever read,
// once, to bring an older pet across; they are never written.
#pragma once
#include "evolve.h"

namespace cp {

// ns: "pet2" for Cell Pet (which may bring the old skeleton pet across), "duck1" for Duck Pet
evo::Save petLoad(const char* ns = "pet2");
void      petStore(evo::Save& s, const char* ns = "pet2");

}  // namespace cp
