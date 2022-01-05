#pragma once

#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace YAML { struct Node; }

namespace kara::cli {
    struct PackageLockFile {
        std::unordered_map<std::string, std::vector<std::string>> packagesInstalled;

        [[nodiscard]] std::string serialize() const;

        PackageLockFile() = default;
        explicit PackageLockFile(const YAML::Node &node);
    };

    struct PackageBuildResult {
        std::vector<std::string> configFiles;
        std::vector<std::string> builtTargets;
    };

    struct PackageManager {
        std::string root;
        fs::path packagesDirectory;

        PackageLockFile lockFile;

        [[nodiscard]] std::ofstream lockFileWriteStream() const; // const?

        // Returns list of .yaml config files, equivalent to import: in TargetConfig

        PackageBuildResult build(const fs::path &root,
            const std::string &name,
            const std::string &suggestTarget = "",
            const std::vector<std::string> &arguments = {});

        // reinstall
        PackageBuildResult download(const std::string &url,
            const std::string &suggestTarget = "",
            const std::vector<std::string> &arguments = {});
        // checks lock file
        std::vector<std::string> install(const std::string &url,
            const std::string &suggestTarget = "",
            const std::vector<std::string> &arguments = {});

        explicit PackageManager(fs::path packagesDirectory, std::string root);
    };
}
