#include "input.h"
#include <cmath>

namespace aether::entisgls {
namespace {
bool Key(int vk, SDL_KeyboardEvent& event) {
    if (vk >= 'A' && vk <= 'Z') {
        event.scancode = SDL_Scancode(SDL_SCANCODE_A + vk - 'A');
        event.key = SDLK_A + vk - 'A';
        return true;
    }
    if (vk >= '0' && vk <= '9') {
        event.scancode = vk == '0' ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + vk - '1');
        event.key = SDLK_0 + vk - '0';
        return true;
    }
    if (vk >= 0x70 && vk <= 0x7b) {
        event.scancode = SDL_Scancode(SDL_SCANCODE_F1 + vk - 0x70);
        event.key = SDLK_F1 + vk - 0x70;
        return true;
    }
    struct Mapping { int vk; SDL_Scancode scan; SDL_Keycode key; };
    static constexpr Mapping mappings[] = {
        {8, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE}, {9, SDL_SCANCODE_TAB, SDLK_TAB},
        {13, SDL_SCANCODE_RETURN, SDLK_RETURN}, {16, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT},
        {17, SDL_SCANCODE_LCTRL, SDLK_LCTRL}, {18, SDL_SCANCODE_LALT, SDLK_LALT},
        {27, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE}, {32, SDL_SCANCODE_SPACE, SDLK_SPACE},
        {33, SDL_SCANCODE_PAGEUP, SDLK_PAGEUP}, {34, SDL_SCANCODE_PAGEDOWN, SDLK_PAGEDOWN},
        {35, SDL_SCANCODE_END, SDLK_END}, {36, SDL_SCANCODE_HOME, SDLK_HOME},
        {37, SDL_SCANCODE_LEFT, SDLK_LEFT}, {38, SDL_SCANCODE_UP, SDLK_UP},
        {39, SDL_SCANCODE_RIGHT, SDLK_RIGHT}, {40, SDL_SCANCODE_DOWN, SDLK_DOWN},
        {45, SDL_SCANCODE_INSERT, SDLK_INSERT}, {46, SDL_SCANCODE_DELETE, SDLK_DELETE}
    };
    for (const auto& mapping : mappings) if (mapping.vk == vk) {
        event.scancode = mapping.scan; event.key = mapping.key; return true;
    }
    return false;
}
bool Utf8(uint32_t cp, std::string& text) {
    text.clear();
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    if (cp < 0x80) text += char(cp);
    else if (cp < 0x800) { text += char(0xc0 | (cp >> 6)); text += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) {
        text += char(0xe0 | (cp >> 12)); text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
    } else {
        text += char(0xf0 | (cp >> 18)); text += char(0x80 | ((cp >> 12) & 63));
        text += char(0x80 | ((cp >> 6) & 63)); text += char(0x80 | (cp & 63));
    }
    return true;
}
}
bool TranslateInput(const engine_input_event_t& input, SDL_Event& out, std::string& text) {
    out = {}; text.clear();
    if (input.struct_size < sizeof(input)) return false;
    out.common.timestamp = input.timestamp_micros * 1000;
    switch (input.type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_UP:
        if (!std::isfinite(input.x) || !std::isfinite(input.y) || input.button < 0 || input.button > 2) return false;
        out.type = input.type == ENGINE_INPUT_EVENT_POINTER_DOWN ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        out.button.button = input.button == 1 ? SDL_BUTTON_RIGHT : input.button == 2 ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
        out.button.down = input.type == ENGINE_INPUT_EVENT_POINTER_DOWN;
        out.button.x = float(input.x); out.button.y = float(input.y); out.button.clicks = 1;
        if (input.modifiers & ENGINE_INPUT_MODIFIER_POINTER_CANCEL) out.button.x = out.button.y = -10000;
        return true;
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
        if (!std::isfinite(input.x) || !std::isfinite(input.y)) return false;
        out.type = SDL_EVENT_MOUSE_MOTION; out.motion.x = float(input.x); out.motion.y = float(input.y);
        return true;
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
        if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.delta_y)) return false;
        out.type = SDL_EVENT_MOUSE_WHEEL; out.wheel.y = float(input.delta_y);
        out.wheel.mouse_x = float(input.x); out.wheel.mouse_y = float(input.y);
        return true;
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_BACK:
        out.type = input.type == ENGINE_INPUT_EVENT_KEY_UP ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
        out.key.down = out.type == SDL_EVENT_KEY_DOWN;
        return Key(input.type == ENGINE_INPUT_EVENT_BACK ? 27 : input.key_code, out.key);
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
        if (!Utf8(input.unicode_codepoint, text)) return false;
        out.type = SDL_EVENT_TEXT_INPUT; out.text.text = text.c_str(); return true;
    default: return false;
    }
}
}
