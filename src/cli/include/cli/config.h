#pragma once

#include <options/options.h>

#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace YAML {
    struct Node;
    struct Emitter;
}

namespace kara::cli {
    struct TargetConfig;

    enum class TargetType {
        Library,
        Executable,
        Interface,
    };

    struct TargetOptions {
        std::vector<std::string> includes;
        std::vector<std::string> includeArguments;

        std::vector<std::string> libraries;
        std::vector<std::string> dynamicLibraries;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        bool operator==(const TargetOptions &other) const;
        bool operator!=(const TargetOptions &other) const;

        void serializeInline(YAML::Emitter &emitter) const;
        void serialize(YAML::Emitter &emitter) const;

        TargetOptions() = default;
        explicit TargetOptions(const YAML::Node &node);
    };

    struct TargetImport {
        std::string from;

        std::vector<std::string> import; // suggested targets names
        std::vector<std::string> buildArguments; // pass to cmake

        TargetOptions options;

        [[nodiscard]] bool direct() const; // used for pretty serialization

        void serialize(YAML::Emitter &emitter) const;

        TargetImport() = default;
        explicit TargetImport(const YAML::Node &node);
    };

    struct TargetConfig {
        fs::path root;

        std::string name;

        TargetType type = TargetType::Library;

        std::set<std::string> files;

        // ? what about defaults for subtargets?
        std::string outputDirectory = "build";
        std::string packagesDirectory = "build";

//        std::vector<TargetConfig> configs;

        std::vector<TargetImport> import;

        TargetOptions options;

        [[nodiscard]] std::string serialize() const;

        [[nodiscard]] std::string resolveName() const;

//        void resolveConfigs(ConfigMap &configs) const;
//        [[nodiscard]] ConfigMap resolveConfigs() const;

        static std::optional<TargetConfig> loadFrom(const std::string &path);
        static TargetConfig loadFromThrows(const std::string &path);

        TargetConfig() = default;
        explicit TargetConfig(fs::path root, const YAML::Node &node);
    };
}
