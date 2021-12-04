#pragma once

#include <options/options.h>

#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace YAML { struct Node; }

namespace kara::cli {
    struct TargetConfig;

    enum class TargetType {
        Library,
        Executable,
        Interface,
    };

    using ConfigMap = std::unordered_map<std::string, const TargetConfig *>;

    struct TargetConfig {
        fs::path root;

        std::string name;

        TargetType type = TargetType::Library;

        std::set<std::string> files;

        // ? what about defaults for subtargets?
        std::string outputDirectory = "build";
        std::string packagesDirectory = "build";

        std::vector<TargetConfig> configs;
        std::set<std::string> import;

        std::unordered_map<std::string, std::vector<std::string>> packages;

        std::vector<std::string> includes;
        std::vector<std::string> includeArguments;

        std::vector<std::string> libraries;
        std::vector<std::string> dynamicLibraries;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        [[nodiscard]] std::string serialize() const;

        [[nodiscard]] std::string resolveName() const;

        void resolveConfigs(ConfigMap &configs) const;
        [[nodiscard]] ConfigMap resolveConfigs() const;

        static std::optional<TargetConfig> loadFrom(const std::string &path);
        static TargetConfig loadFromThrows(const std::string &path);

        TargetConfig() = default;
        explicit TargetConfig(fs::path root, const YAML::Node &node);
    };
}
