#pragma once

#include <cli/lock.h>
#include <cli/utility.h>

#include <set>
#include <string>
#include <vector>
#include <unordered_map>

namespace YAML { struct Node; }

namespace kara::cli {
    struct Platform;

    struct PackageBuildResult {
        std::vector<std::string> configFiles;
        std::vector<std::string> builtTargets;
    };

    struct PackageManager {
        std::string root;
        Platform &platform;

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

        PackageBuildResult buildCMakePackage(const std::string &name);

        explicit PackageManager(Platform &platform, fs::path packagesDirectory, std::string root);
    };
}
