#pragma once

#include <cli/config.h>

#include <builder/manager.h>

namespace kara::cli {
    struct TargetResult {
        std::unordered_set<std::string> libraries;
        std::unordered_set<std::string> external;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        std::unique_ptr<llvm::Module> module;
    };

    struct ProjectManager {
        ProjectConfig config;
        kara::builder::Manager manager; // unique_ptr?

        std::unordered_map<std::string, std::unique_ptr<TargetResult>> updatedTargets;

        fs::path createTargetDirectory(const std::string &target) const;

        const TargetResult &makeTarget(
            const std::string &target, const std::string &root, const std::string &linkerType = "");

        ProjectManager(ProjectConfig config, const std::string &triple);
    };
}

