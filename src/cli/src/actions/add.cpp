#include <cli/cli.h>

#include <cli/config.h>
#include <cli/packages.h>

#include <fstream>

namespace kara::cli {
    void CLIAddOptions::execute() {
        auto config = TargetConfig::loadFromThrows(projectFile);

        PackageManager manager(config.packagesDirectory, root);

        auto result = manager.download(url, "", arguments);

        config.packages[url] = std::move(result.builtTargets);

        if (!noWrite) {
            std::ofstream stream(projectFile);
            if (!stream.is_open())
                throw std::runtime_error("Failed to open project file for writing.");

            stream << config.serialize();
        }
    }
}
