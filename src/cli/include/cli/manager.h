#pragma once

#include <cli/config.h>

#include <builder/target.h>
#include <builder/manager.h>

#include <llvm/IR/Module.h>

namespace kara::cli {
    struct TargetResult {
        std::vector<std::string> libraries;
//        std::unordered_set<std::string> external;

        std::vector<std::string> linkerOptions;

        kara::options::Options defaultOptions;

        std::unique_ptr<llvm::Module> module;
    };

    struct ProjectManager {
        builder::Target builderTarget;
        TargetConfig main;
        ConfigMap configs;
        builder::SourceDatabase database;

        std::unordered_map<std::string, std::unique_ptr<TargetResult>> updatedTargets;

        fs::path createTargetDirectory(const std::string &target);

        const TargetResult &makeTarget(
            const std::string &target, const std::string &root, const std::string &linkerType = "");

        ProjectManager(TargetConfig main, const std::string &triple);
    };
}

