#include <gls.h>
#include "runtime/cotopha_port/legacy_environment.h"
#include "platform/log.h"

namespace {
SSystem::SString WritablePath(const SSystem::SString &path, bool allWritable) {
    if (allWritable) return path;
    auto result = path;
    const auto scheme = result.Find(L':');
    if (scheme >= 0) result = result.Middle(static_cast<size_t>(scheme) + 1);
    while (!result.IsEmpty()) {
        const auto first = static_cast<const wchar_t *>(result)[0];
        if (first != L'/' && first != L'\\') break;
        result = result.Middle(1);
    }
    return result;
}
// Keep relative reads (for example an ERI reference image) in the game's
// environment, rather than resolving them beside a materialized override.
class ResourceFile final : public SSystem::SSmartFile {
    const ECSEnvironment &environment_;
    SSystem::SString path_;
    std::shared_ptr<SSystem::SFileOpener> owner_;
public:
    ResourceFile(const ECSEnvironment &environment, const wchar_t *path,
                 SSystem::SFileInterface *file, std::shared_ptr<SSystem::SFileOpener> owner)
        : SSmartFile(nullptr, file), environment_(environment), path_(path), owner_(std::move(owner)) {}
    SSystem::SFileInterface *NewOpenFile(const wchar_t *path, long flags) override {
        if (flags & SSystem::SFileOpener::modeWriteFlag) return nullptr;
        // Archive references may already be game-relative. Loose resources may
        // instead name a sibling, so try that after the native logical lookup.
        if (auto *file = environment_.NewOpenFile(path, flags)) return file;
        auto relative = path_.GetFileDirectoryPart(L'/').OffsetFilePath(path);
        if (auto *file = environment_.NewOpenFile(relative, flags)) return file;
        const std::wstring name(static_cast<const wchar_t *>(relative));
        constexpr const wchar_t *gamePrefix = L"storage://game/";
        return name.rfind(gamePrefix, 0) == 0 ? environment_.NewOpenFile(name.c_str() + 15, flags) : nullptr;
    }
    SSystem::SFileInterface *Duplicate() const override {
        auto *copy = m_pFile->Duplicate();
        return copy ? new ResourceFile(environment_, path_, copy, owner_) : nullptr;
    }
};
}

SSystem::SFileInterface *ECSEnvironment::NewOpenFile(const wchar_t *path, long flags) {
    return static_cast<const ECSEnvironment &>(*this).NewOpenFile(path, flags);
}
SSystem::SFileInterface *ECSEnvironment::NewOpenFile(const wchar_t *path, long flags) const {
    auto *writable = const_cast<ECSEnvironment *>(this)->GetWritableFileOpener();
    SSystem::SString filtered(path);
    const_cast<ECSEnvironment *>(this)->FilterEnvironmentString(filtered);
    if (overrides_ && !(flags & SSystem::SFileOpener::modeWriteFlag) &&
        !(writable && writable->IsExisting(WritablePath(filtered, m_flagAllFileWritable))))
        if (auto *file = overrides_->NewOpenFile(filtered, flags))
            return new ResourceFile(*this, filtered, file, overrides_);
    return ECSSakura2::EnvironmentVM::NewOpenFile(path, flags);
}
bool ECSEnvironment::IsExistingFile(const wchar_t *path) {
    SSystem::SString filtered(path);
    FilterEnvironmentString(filtered);
    return (overrides_ && overrides_->IsExisting(filtered)) || SSystem::SEnvironment::IsExistingFile(path);
}
bool ECSEnvironment::IsExistingFile(const wchar_t *path) const {
    SSystem::SString filtered(path);
    const_cast<ECSEnvironment *>(this)->FilterEnvironmentString(filtered);
    return (overrides_ && overrides_->IsExisting(filtered)) || ECSSakura2::EnvironmentVM::IsExistingFile(path);
}
SSystem::SError ECSEnvironment::QueryFileState(const wchar_t *path, SSystem::SFileOpener::State &state) {
    SSystem::SString filtered(path);
    FilterEnvironmentString(filtered);
    if (overrides_ && overrides_->IsExisting(filtered)) {
        auto *writable = GetWritableFileOpener();
        if (writable && writable->QueryState(WritablePath(filtered, m_flagAllFileWritable), state) == SSystem::errSuccess)
            return SSystem::errSuccess;
        return overrides_->QueryState(filtered, state);
    }
    return SSystem::SEnvironment::QueryFileState(path, state);
}

SSystem::SError ECSEnvironment::InitEnvironment() {
    const auto result = ECSSakura2::EnvironmentVM::InitEnvironment();
    RegisterEnvironmentString(L"CURRENT", L"storage://game");
    return result;
}

EWideString ECSEnvironment::FilterFilePath(const wchar_t *path) {
    SSystem::SString value(path);
    FilterEnvironmentString(value);
    return EWideString(static_cast<const wchar_t *>(value));
}

ESLFileObject *ECSEnvironment::OpenFileObject(const char *name, long flags) {
    if (!name) return nullptr;
    const EWideString wideName(name);
    const EWideString filtered = FilterFilePath(wideName);
    SSystem::SEnvironment::AcceptAllFileForWriting(m_fAcceptOtherSaveDir);
    auto *file = NewOpenFile(filtered, flags);
    return file ? new LegacyFileAdapter(file, flags) : nullptr;
}

ESLError ECSEnvironment::AddFileArchive(const wchar_t *path, const char *password,
                                      const wchar_t *id) {
    if (!path) return eslErrInvalidParam;
    const EWideString filtered = FilterFilePath(path);
    const EWideString widePassword(password ? password : "");
    auto *opener = CreateArchiveOpener(filtered, widePassword);
    if (!opener) return eslErrGeneral;
    // SEnvironment retains ownership and appends, matching the original GLS3
    // file search precedence. Dynamic patches stay in script-defined order.
    AddFileOpener(opener, id ? id : L"");
    study::platform::LogPrint(study::platform::LogPriority::Info, "StudySteady", "Legacy archive mounted: %s",
        SSystem::SString(static_cast<const wchar_t *>(filtered)).ToCharArray().GetConstArray());
    return eslErrSuccess;
}

ESLError ECSEnvironment::EnableFilePath(const wchar_t *id, bool enable) {
    if (!id || FindFileOpenerAs(id) < 0) return eslErrGeneral;
    EnableFileOpener(id, enable);
    return eslErrSuccess;
}

ESLError ECSEnvironment::AddModule(const wchar_t *name, ECSContext *) {
    study::platform::LogPrint(study::platform::LogPriority::Error, "StudySteady", "Windows Cotopha plugin unsupported: %s",
        SSystem::SString(name ? name : L"").ToCharArray().GetConstArray());
    return eslErrNotSupported;
}

FARPROC ECSEnvironment::FindPluginedFunction(const char *) {
    // No binary plug-ins have been accepted by AddModule.
    return nullptr;
}

void ECSExecutionImage::AttachCSEnvironment(ECSEnvironment *environment) {
    m_pEnv = environment;
    ECSSakura2::StandardVM::AttachEnvironment(environment);
}
