#include "font_discovery.h"
#include <algorithm>
#include <cctype>
#include <system_error>

namespace aether::entisgls {
std::vector<std::string> DiscoverBundledFonts(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<std::string> paths;
    std::error_code error;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
    // Bound startup cost even when a user selects a very large directory.
    size_t visited = 0;
    for (; it != end && !error && visited++ < 32768; it.increment(error)) {
        const auto status = it->symlink_status(error);
        if (error) break;
        if (fs::is_symlink(status)) { it.disable_recursion_pending(); continue; }
        if (fs::is_directory(status)) {
            const auto name = it->path().filename().u8string();
            if (it.depth() >= 3 || (!name.empty() && name.front() == '.')) it.disable_recursion_pending();
            continue;
        }
        if (!fs::is_regular_file(status)) continue;
        auto extension = it->path().extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".otf" && extension != ".ttf" && extension != ".ttc" && extension != ".otc") continue;
        const auto size = it->file_size(error);
        if (!error && size > 0 && size <= 64u * 1024u * 1024u)
            paths.push_back(it->path().lexically_relative(root).generic_u8string());
    }
    std::sort(paths.begin(), paths.end());
    if (paths.size() > 128) paths.resize(128);
    return paths;
}
}
