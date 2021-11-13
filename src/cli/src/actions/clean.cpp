#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>

namespace kara::cli {
    void CLICleanOptions::execute() {
        auto config = ProjectConfig::loadFromThrows(projectFile);

        fs::remove_all(config.outputDirectory);

        log(LogSource::target, "Cleaned");
    }
}
