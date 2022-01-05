#pragma once

#include <cli/config.h>
#include <cli/packages.h>

#include <builder/target.h>
#include <builder/manager.h>

#include <llvm/IR/Module.h>

namespace kara::cli {
    struct TargetInfo {
        std::vector<const TargetConfig *> depends;

        std::vector<std::string> libraries;
        std::vector<std::string> dynamicLibraries;

        std::vector<builder::Library> includes;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;
    };

    struct TargetResult {
        const TargetInfo &info;

        std::unique_ptr<llvm::Module> module;
    };

    // or database?
    struct TargetCache {
        std::vector<std::unique_ptr<TargetConfig>> configHold;

        std::unordered_map<std::string, const TargetConfig *> configsByName;
        std::unordered_map<std::string, const TargetConfig *> configsByPath;
        std::unordered_map<std::string, const TargetConfig *> configsByUrl;

        const TargetConfig *resolveImport(const TargetConfig &parent, const TargetImport &import) const;

        void add(const TargetConfig &config, PackageManager &packages);
    };

    struct ProjectManager {
        builder::Target builderTarget;
        builder::SourceDatabase sourceDatabase;

        TargetConfig mainTarget;

        PackageManager packageManager;

        TargetCache targetCache; // after pm in initialization

        std::unordered_map<const TargetConfig *, std::unique_ptr<TargetInfo>> targetInfos;
        std::unordered_map<const TargetConfig *, std::unique_ptr<TargetResult>> updatedTargets;

        fs::path createTargetDirectory(const std::string &target) const;

        const TargetConfig *getTarget(const std::string &name);

        const TargetInfo &readTarget(const TargetConfig *target);

        const TargetResult &makeTarget(
            const TargetConfig *target, const std::string &root, const std::string &linkerType = "");

        ProjectManager(const TargetConfig &main, const std::string &triple, const std::string &root);
    };
}

