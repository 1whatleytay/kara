#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>
#include <cli/manager.h>

#include <unistd.h>

namespace kara::cli {
    void CLIRunOptions::execute() {

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

        auto it = config->targets.find(targetToBuild);
        if (it == config->targets.end())
            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));

        if (it->second.type != TargetType::Executable)
            throw std::runtime_error(fmt::format("Target {} does not have executable type.", target));

        manager.makeTarget(targetToBuild, root, linkerType);

        auto directory = manager.createTargetDirectory(targetToBuild);
        auto executable = directory / target; // ?

        log(LogSource::target, "Running {}", targetToBuild);

        auto process = fork();

        if (!process) {
            std::vector<char *const> arguments = { nullptr };
            execv(executable.string().c_str(), arguments.data());

            exit(0);
        }

        int status {};
        waitpid(process, &status, 0);

        log(LogSource::target, "Finished");
    }
}
