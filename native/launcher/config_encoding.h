#pragma once
#include <string>
namespace entis::launcher {
// Decode the XML-declared encoding before the bounded configuration parser.
// No process locale or game-specific encoding default is changed.
std::string DecodeConfigurationText(const std::string& input, std::size_t maximumBytes = 1024 * 1024);
}
