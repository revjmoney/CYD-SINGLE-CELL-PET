// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The whole Cell Pet, as one call: it takes over the board and never returns.
#pragma once

namespace cp {
[[noreturn]] void petMain();  // call once the panel (boardInit) and Serial are up
}
