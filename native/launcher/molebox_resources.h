#pragma once
#include "io/game_files.h"

namespace entis::launcher {
// Reads the legacy Molebox virtual filesystem without executing any PE code.
// Ordinary executables return an empty layer; malformed recognized packages
// fail instead of silently falling back to unpatched resources.
io::ReadOnlyFiles ReadMoleboxResources(const std::vector<std::uint8_t>& executable);
}
