#pragma once

#include <options/options.h>

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace YAML { struct Node; }

namespace kara::cli {
    enum class TargetType {
        Library,
        Executable,
        Interface,
    };

    struct TargetConfig {
        TargetType type = TargetType::Library;

        std::unordered_set<std::string> files;
        std::unordered_set<std::string> libraries;
        std::unordered_set<std::string> external;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        TargetConfig() = default;
        explicit TargetConfig(const YAML::Node &node);
    };

    struct ProjectConfig {
        std::string defaultTarget;
        std::string outputDirectory = "build";
        std::string packagesDirectory = "build";

        std::unordered_map<std::string, TargetConfig> targets;

        static std::optional<ProjectConfig> loadFrom(const std::string &path);
        static ProjectConfig loadFromThrows(const std::string &path);

        ProjectConfig() = default;
        explicit ProjectConfig(const YAML::Node &node);
    };
}
