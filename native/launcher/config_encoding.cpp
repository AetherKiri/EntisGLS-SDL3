#include "launcher/config_encoding.h"
#include "launcher/game_config.h"
#include <sakuraglx/sakuraglx.h>
#include <esl/esl_charset.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace entis::launcher {
namespace {
void Append(std::string& text, uint32_t cp) {
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw ConfigError("Invalid Unicode in launch configuration");
    if (cp < 0x80) text += char(cp);
    else if (cp < 0x800) { text += char(0xc0 | (cp >> 6)); text += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) {
        text += char(0xe0 | (cp >> 12)); text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
    } else {
        text += char(0xf0 | (cp >> 18)); text += char(0x80 | ((cp >> 12) & 63));
        text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
    }
}
struct Declaration { size_t at = 0, length = 0; std::string encoding; };
Declaration Encoding(const std::string& input) {
    Declaration result;
    const size_t start = input.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    if (input.compare(start, 5, "<?xml")) return result;
    const auto end = input.find("?>", start);
    if (end == std::string::npos || end > 1024) throw ConfigError("Invalid XML declaration");
    auto declaration = input.substr(start, end - start);
    for (auto& ch : declaration) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto at = declaration.find("encoding");
    if (at == std::string::npos) return result;
    at += 8;
    while (at < declaration.size() && std::isspace(static_cast<unsigned char>(declaration[at]))) ++at;
    if (at == declaration.size() || declaration[at++] != '=') throw ConfigError("Invalid XML encoding declaration");
    while (at < declaration.size() && std::isspace(static_cast<unsigned char>(declaration[at]))) ++at;
    if (at == declaration.size() || (declaration[at] != '\'' && declaration[at] != '"')) throw ConfigError("Invalid XML encoding declaration");
    const auto quote = declaration[at++]; const auto finish = declaration.find(quote, at);
    if (finish == std::string::npos) throw ConfigError("Invalid XML encoding declaration");
    result.at = start + at; result.length = finish - at; result.encoding = declaration.substr(at, result.length);
    return result;
}
}
std::string DecodeConfigurationText(const std::string& input) {
    if (input.size() > 1024 * 1024) throw ConfigError("XML configuration exceeds 1 MiB");
    std::string text = input;
    bool utf16 = false, littleEndian = false;
    if (text.size() >= 2 && ((uint8_t(text[0]) == 0xff && uint8_t(text[1]) == 0xfe) ||
                            (uint8_t(text[0]) == 0xfe && uint8_t(text[1]) == 0xff))) {
        utf16 = true; const bool little = uint8_t(text[0]) == 0xff; littleEndian = little;
        if (text.size() % 2) throw ConfigError("Truncated UTF-16 launch configuration");
        auto word = [&](size_t at) { return uint32_t(uint8_t(input[at + (little ? 0 : 1)])) |
            (uint32_t(uint8_t(input[at + (little ? 1 : 0)])) << 8); };
        text.clear();
        for (size_t at = 2; at < input.size(); at += 2) {
            auto cp = word(at);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (at + 3 >= input.size()) throw ConfigError("Truncated UTF-16 surrogate");
                const auto low = word(at + 2);
                if (low < 0xdc00 || low > 0xdfff) throw ConfigError("Invalid UTF-16 surrogate");
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00; at += 2;
            }
            Append(text, cp);
        }
    }
    const auto declaration = Encoding(text);
    const auto& enc = declaration.encoding;
    if (utf16) {
        if (!enc.empty() && enc != "utf-16" && enc != "utf-16le" && enc != "utf-16be")
            throw ConfigError("Conflicting XML encoding and UTF-16 BOM");
        if ((enc == "utf-16le" && !littleEndian) || (enc == "utf-16be" && littleEndian))
            throw ConfigError("Conflicting UTF-16 byte order and BOM");
    } else if (enc == "shift_jis" || enc == "shift-jis" || enc == "sjis" || enc == "cp932" || enc == "windows-31j") {
        for (size_t i=0;i<text.size();++i) {
            const auto lead=uint8_t(text[i]);
            if ((lead>=0x81&&lead<=0x9f)||(lead>=0xe0&&lead<=0xfc)) {
                if (++i==text.size()) throw ConfigError("Truncated Shift_JIS character");
                const auto trail=uint8_t(text[i]);
                if (trail<0x40||trail>0xfc||trail==0x7f) throw ConfigError("Invalid Shift_JIS character");
            }
        }
        std::vector<wchar_t> wide(text.size() + 1);
        const auto length = ESLCharset::ShiftJIStoUNICODE(reinterpret_cast<const unsigned char*>(text.data()),
                                                         unsigned(text.size()), wide.data(), unsigned(wide.size()));
        if (!length) throw ConfigError("Invalid Shift_JIS launch configuration");
        text.clear();
        for (unsigned i = 0; i < length; ++i) Append(text, uint32_t(wide[i]));
    } else if (!enc.empty() && enc != "utf-8" && enc != "utf8") {
        throw ConfigError("Unsupported launch configuration encoding: " + enc);
    }
    if (declaration.length) text.replace(declaration.at, declaration.length, "utf-8");
    return text;
}
}
