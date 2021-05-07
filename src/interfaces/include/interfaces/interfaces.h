#pragma once

#include <parser/root.h>

#include <hermes/state.h>

#include <memory>

using InterfaceResult = std::tuple<std::unique_ptr<hermes::State>, std::unique_ptr<RootNode>>;

namespace interfaces::header {
    InterfaceResult create(int count, const char **args);
}

// No other interfaces yet...
