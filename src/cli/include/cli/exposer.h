#pragma once

namespace YAML {
    struct Emitter;
}

namespace kara::parser {
    struct Root;
}

namespace kara::cli {
    void expose(const parser::Root *root, YAML::Emitter &emitter);
}