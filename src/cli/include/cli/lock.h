#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace YAML {
    struct Node;
}

namespace kara::cli {
    struct PackageLockFile {
        std::unordered_map<std::string, std::vector<std::string>> packagesInstalled;

        static std::string createPath(const std::string &parent);

        [[nodiscard]] std::string serialize() const;

        PackageLockFile() = default;
        explicit PackageLockFile(const YAML::Node &node);
    };

    struct BuildLockFile {
        // keys -> values for important data build to build
        std::unordered_map<std::string, std::string> parameters;

        static std::string createPath(const std::string &parent);

        [[nodiscard]] std::string serialize() const;

        BuildLockFile() = default;
        explicit BuildLockFile(const YAML::Node &node);
    };
}
