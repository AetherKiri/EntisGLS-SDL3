#pragma once
#include <functional>
#include <nlohmann/json.hpp>
class ECSEnvironment;
namespace entis::launcher { struct GameLaunchConfig; }
namespace entis::witch {
int Run(ECSEnvironment& environment, const launcher::GameLaunchConfig& launch,
        const std::function<bool()>& cancelled);
nlohmann::json Snapshot();
}
