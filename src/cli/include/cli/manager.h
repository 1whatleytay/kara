#pragma once

#include <cli/config.h>
#include <cli/packages.h>

#include <builder/target.h>
#include <builder/manager.h>

#include <llvm/IR/Module.h>

namespace kara::cli {
    struct TargetResult {
        std::vector<std::string> depends;

        std::vector<std::string> libraries;
        std::vector<std::string> dynamicLibraries;

        std::vector<builder::Library> includes;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        std::unique_ptr<llvm::Module> module;
    };

    // unfortunate...
    using ConfigHold = std::vector<std::unique_ptr<TargetConfig>>;
    void tracePackages(const TargetConfig &config, PackageManager &packages, ConfigHold &hold, ConfigMap &out);

    struct ProjectManager {
        builder::Target builderTarget;
        builder::SourceDatabase database;

        TargetConfig main;
        ConfigMap configs;

        ConfigHold packageConfigs;
        PackageManager packages;

        std::unordered_map<std::string, std::unique_ptr<TargetResult>> updatedTargets;

        fs::path createTargetDirectory(const std::string &target);

        const TargetResult &makeTarget(
            const std::string &target, const std::string &root, const std::string &linkerType = "");

        ProjectManager(const TargetConfig &main, const std::string &triple, const std::string &root);
    };
}

