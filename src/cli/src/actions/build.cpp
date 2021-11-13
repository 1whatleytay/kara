#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>
#include <cli/manager.h>

namespace kara::cli {
    void CLIBuildOptions::execute() {
        auto config = ProjectConfig::loadFrom(projectFile);

        if (!config) {
            auto path = fs::absolute(fs::path(projectFile)).string();

            throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
        }

        ProjectManager manager(*config, triple);

        std::string targetToBuild = target;

        if (targetToBuild.empty())
            targetToBuild = config->defaultTarget;

        if (targetToBuild.empty())
            throw std::runtime_error("Target to build must be specified over command line.");

        auto &result = manager.makeTarget(targetToBuild, root, linkerType);

        if (printIr) {
            fmt::print("Printing IR...\n");
            result.module->print(llvm::outs(), nullptr);
        }
    }
}
