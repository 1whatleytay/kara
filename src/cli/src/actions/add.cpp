#include <cli/cli.h>

#include <cli/config.h>
#include <cli/packages.h>
#include <cli/platform.h>

#include <yaml-cpp/yaml.h>

#include <fstream>

namespace kara::cli {
    void CLIAddOptions::execute() {
        auto config = TargetConfig::loadFromThrows(projectFile);

        // this will annoy someone
        BuildLockFile lock(YAML::LoadFile(BuildLockFile::createPath(config.outputDirectory)));
        auto platform = Platform::byNative(root, lock);

        PackageManager manager(*platform, config.packagesDirectory, root);

        auto result = manager.download(url, "", arguments);

        TargetImport import;
        import.kind = TargetImportKind::RepositoryUrl;
        import.path = url;
        import.targets = std::move(result.builtTargets); // unfortunate
        import.buildArguments = arguments;

        config.import.push_back(import);

        if (!noWrite) {
            std::ofstream stream(projectFile);
            if (!stream.is_open())
                throw std::runtime_error("Failed to open project file for writing.");

            stream << config.serialize();

            std::ofstream lockStream(BuildLockFile::createPath(config.outputDirectory));
            if (lockStream.is_open())
                lockStream << lock.serialize();
        }
    }
}
