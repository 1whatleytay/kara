#pragma once

#include <cli/config.h>

#include <set>
#include <string>

namespace YAML { struct Node; }

namespace kara::cli {
    struct PackageLockFile {
        std::unordered_map<std::string, std::vector<std::string>> packagesInstalled;

        [[nodiscard]] std::string serialize() const;

        PackageLockFile() = default;
        explicit PackageLockFile(const YAML::Node &node);
    };

    struct PackageDownloadResult {
        std::vector<std::string> configFiles;
        std::vector<std::string> builtTargets;
    };

    struct PackageManager {
        std::string root;
        fs::path packagesDirectory;

        PackageLockFile lockFile;

        [[nodiscard]] std::ofstream lockFileWriteStream() const; // const?

        // Returns list of .yaml config files, equivalent to import: in TargetConfig

        // reinstall
        PackageDownloadResult download(const std::string &url,
            const std::string &suggestTarget = "",
            const std::vector<std::string> &arguments = {});
        // checks lock file
        std::vector<std::string> install(const std::string &url,
            const std::string &suggestTarget = "",
            const std::vector<std::string> &arguments = {});

        explicit PackageManager(fs::path packagesDirectory, std::string root);
    };
}
