#include "game_hooks.h"
#include <sakura/sakura.h>
#include <set>

namespace aether::entisgls {
namespace {
namespace fs = std::filesystem;
using namespace SSystem;
SString Wide(const std::string &text) {
    SString value;
    value.FromUTF8(reinterpret_cast<const uint8_t *>(text.c_str()));
    return value;
}
class ResourceOverrides final : public SFileOpener {
    const std::shared_ptr<hooks::GameHooks> hooks_;
    const fs::path root_, cache_;
    SStandardFileOpener files_;
    fs::path Resolve(const wchar_t *path) const {
        if (!path) return {};
        const auto utf8 = SString(path).ToUTF8();
        std::string name(reinterpret_cast<const char *>(utf8.GetConstArray()), utf8.GetLength());
        if (!name.empty() && name.back() == '\0') name.pop_back();
        for (char &ch : name) if (ch == '\\') ch = '/';
        if (name.rfind("storage://game/", 0) == 0) name.erase(0, 15);
        else {
            if (name.rfind("file://", 0) == 0) name.erase(0, 7);
            const auto native = fs::u8path(name);
            if (native.is_absolute()) name = native.lexically_relative(root_).generic_u8string();
        }
        try { name = hooks::GameHooks::ResourceKey(name); }
        catch (const std::exception &) { return {}; }
        // Keep both explicit save paths and the write path outside this layer.
        if (name == "savedata" || name.rfind("savedata/", 0) == 0 || !hooks_->Find(name)) return {};
        return cache_ / fs::u8path(name);
    }
public:
    ResourceOverrides(fs::path root, fs::path cache, std::shared_ptr<hooks::GameHooks> hooks)
        : hooks_(std::move(hooks)), root_(std::move(root)), cache_(hooks_->CacheDirectory(cache)) {}
    SFileInterface *NewOpenFile(const wchar_t *path, long flags) override {
        if (flags & modeWriteFlag) return nullptr;
        const auto resolved = Resolve(path);
        return resolved.empty() ? nullptr : files_.NewOpenFile(Wide(resolved.u8string()), flags);
    }
    bool IsExisting(const wchar_t *path) override { return !Resolve(path).empty(); }
    SError QueryState(const wchar_t *path, State &state) override {
        const auto resolved = Resolve(path);
        return resolved.empty() ? errFailed : files_.QueryState(Wide(resolved.u8string()), state);
    }
    void ListSubFiles(SObjectArray<SString> &files, const wchar_t *path) override { List(files, path, false); }
    void ListSubDirectories(SObjectArray<SString> &dirs, const wchar_t *path) override { List(dirs, path, true); }
private:
    void List(SObjectArray<SString> &out, const wchar_t *path, bool directories) {
        std::string prefix;
        if (path && *path) {
            const auto text = SString(path).ToUTF8();
            prefix.assign(reinterpret_cast<const char *>(text.GetConstArray()), text.GetLength());
            if (!prefix.empty() && prefix.back() == '\0') prefix.pop_back();
            try { prefix = hooks::GameHooks::ResourceKey(prefix) + "/"; }
            catch (const std::exception &) { return; }
        }
        std::set<std::string> names;
        for (const auto &[key, bytes] : hooks_->Files()) {
            if (key.rfind(prefix, 0) != 0 || key.rfind("savedata/", 0) == 0) continue;
            const auto name = key.substr(prefix.size());
            const auto slash = name.find('/');
            if ((slash != std::string::npos) == directories) names.insert(name.substr(0, slash));
        }
        for (const auto &name : names) out.Add(new SString(Wide(name)));
    }
};
}
void ApplyGameHooks(const std::filesystem::path &root, const std::filesystem::path &cache,
                    const std::shared_ptr<hooks::GameHooks> &hooks,
                    entis::launcher::GameLaunchConfig &config) {
    // CSX has its own Unicode string representation, not an engine-wide byte
    // encoding switch. Fail explicit unsupported settings instead of ignoring them.
    hooks->RequireEncoding({});
    config.resourceOverrides.reset();
    config.fontOverride.clear();
    if (!hooks->Files().empty())
        config.resourceOverrides = std::make_shared<ResourceOverrides>(root, cache, hooks);
    if (!hooks->Font().empty())
        config.fontOverride = "storage://game/" + hooks->Font().lexically_relative(root).generic_u8string();
}
}
