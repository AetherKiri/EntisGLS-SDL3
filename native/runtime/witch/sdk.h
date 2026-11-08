#pragma once
#include "compatibility/sdk/legacy/gls.h"
#include "runtime/witch/script.h"
#include "launcher/config_encoding.h"
#include <codecvt>
#include <locale>
#include <stdexcept>

namespace entis::witch::sdk {
using namespace SakuraGL;
using namespace SSystem;
// GPU-backed children can release the UI lock while their textures are being
// destroyed on SDL's main thread. Detach the entire tree first, so a concurrent
// frame cannot traverse an object whose destructor is already running.
inline void ClearChildren(SGLSprite& parent) {
    std::vector<SGLSprite*> children;
    for (std::size_t i = 0; i < parent.GetChildCount(); ++i) children.push_back(parent.GetChildAt(i));
    parent.DetachAllChildren();
    for (auto* child : children) delete child;
}
inline SString Wide(const std::string& text) {
    // SString stores UTF-16 even on platforms with a 32-bit wchar_t. Its legacy
    // FromUTF8 narrows supplementary scalars on those platforms.
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const auto units = converter.from_bytes(text);
    SString value; value.SetString(units.data(), units.size()); return value;
}
inline std::string Utf8(const SString& text) {
    if (text.IsEmpty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const wchar_t* units = text;
    return converter.to_bytes(units, units + text.GetLength());
}
[[noreturn]] inline void Fail(const std::string& message) { throw std::runtime_error("WitchWizard: " + message); }
inline Command Convert(const SXMLDocument& node, unsigned depth = 0) {
    if (depth > 64) Fail("script XML nesting exceeds 64");
    Command result; result.tag = Utf8(node.GetTag());
    for (std::size_t i = 0; i < node.GetAttributeCount(); ++i)
        result.attributes[Utf8(*node.GetAttributeNameAt(i))] = Utf8(*node.GetAttributeValueAt(i));
    for (std::size_t i = 0; i < node.GetElementsCount(); ++i) {
        auto* child = node.GetElementAt(i);
        if (child && child->GetType() == SXMLDocument::typeTag) result.children.push_back(Convert(*child, depth + 1));
    }
    return result;
}
inline Module ReadModule(SFileInterface& file, const std::string& name) {
    if (file.GetLength() < 0 || file.GetLength() > 16 * 1024 * 1024) Fail("invalid script length: " + name);
    std::string bytes(static_cast<std::size_t>(file.GetLength()), '\0');
    if (file.Read(bytes.data(), bytes.size()) != bytes.size()) Fail("truncated script " + name);
    const auto xml = Wide(launcher::DecodeConfigurationText(bytes, 16 * 1024 * 1024));
    SXMLDocument document;
    if (document.ParseDocumentFromString(xml, &document)) Fail("invalid XML script " + name);
    auto* root = document.GetElementTagAs(L"xscript");
    auto* code = root ? root->GetElementTagAs(L"code") : nullptr;
    if (!code) Fail("expected xscript/code in " + name);
    return {Convert(*code).children};
}
} // namespace entis::witch::sdk
