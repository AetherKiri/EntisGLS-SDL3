#pragma once

#include "launcher/game_config.h"

namespace entis::launcher {

// Last-resort discovery for games without a readable launch manifest. Uses
// bounded NOA metadata reads and checks the compiled entry header before making
// a minimal in-memory configuration. Never extracts into the game directory.
// Missing script.noa or script.csx returns nullopt; malformed archives throw.
std::optional<GameLaunchConfig> DiscoverArchiveFallback(const std::filesystem::path& root);

// Native WitchWizard uses a fixed main.srcxml entry in the declared archives,
// rather than a CSX src attribute. Identification checks the decoded xscript
// header, not merely the executable's generic IDR_COTOMI environment resource.
bool HasWitchEntry(const std::vector<std::filesystem::path>& archives);

} // namespace entis::launcher
