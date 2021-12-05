#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>
#include <cli/manager.h>

#include <unistd.h>

namespace kara::cli {
    void CLIRunOptions::execute() {
        auto config = TargetConfig::loadFrom(projectFile);

        if (!config) {
            auto path = fs::absolute(fs::path(projectFile)).string();

            throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
        }

        ProjectManager manager(*config, triple, root); // massive copy here :(

        std::string targetToBuild = target;

        if (targetToBuild.empty())
            targetToBuild = config->resolveName();

        if (targetToBuild.empty())
            throw std::runtime_error("Target to build must be specified over command line.");

        auto it = manager.configs.find(targetToBuild);
        if (it == manager.configs.end())
            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", targetToBuild));

        if (it->second->type != TargetType::Executable)
            throw std::runtime_error(fmt::format("Target {} does not have executable type.", targetToBuild));

        manager.makeTarget(targetToBuild, root, linkerType);

        auto directory = manager.createTargetDirectory(targetToBuild);
        auto executable = directory / targetToBuild; // ?

        log(LogSource::target, "Running {}", targetToBuild);

        auto process = fork();

        if (!process) {
            std::vector<char *> arguments = { nullptr };
            execv(executable.string().c_str(), arguments.data());

            exit(1);
        }

        int status {};
        waitpid(process, &status, 0);

        log(LogSource::target, "Finished");
    }
}
