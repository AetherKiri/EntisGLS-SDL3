#pragma once
#include <filesystem>

namespace aether::entisgls {
// Both SDK save URL schemes use the selected game's savedata directory.
// Cache files remain in the host-provided cache, separate from saved games.
void ConfigureGameStorage(const std::filesystem::path& gameRoot,
                          const std::filesystem::path& cacheRoot);
}
