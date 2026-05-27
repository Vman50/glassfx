#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <string>

SDispatchResult dispatchSet(std::string args);
SDispatchResult dispatchParam(std::string args);
SDispatchResult dispatchReload(std::string args);
SDispatchResult dispatchClear(std::string args);
SDispatchResult dispatchList(std::string args);
