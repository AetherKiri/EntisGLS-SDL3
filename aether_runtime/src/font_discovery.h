#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace aether::entisgls {
// Relative paths only. Never follow symlinks or scan outside the selected game.
std::vector<std::string> DiscoverBundledFonts(const std::filesystem::path& root);
}
