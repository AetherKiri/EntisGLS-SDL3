#pragma once
#include "engine_api.h"
#include <SDL3/SDL_events.h>
#include <string>

namespace aether::entisgls {
bool TranslateInput(const engine_input_event_t& input, SDL_Event& output, std::string& text);
}
