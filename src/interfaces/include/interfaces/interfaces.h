#pragma once

#include <parser/root.h>

#include <hermes/state.h>

#include <memory>

namespace kara::interfaces {
    using InterfaceResult = std::tuple<std::unique_ptr<hermes::State>, std::unique_ptr<parser::Root>>;
}

namespace kara::interfaces::header {
    InterfaceResult create(int count, const char **args);
}

// No other interfaces yet...
