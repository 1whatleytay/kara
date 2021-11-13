#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>

#include <pugixml.hpp>

#include <unistd.h>

namespace kara::cli {
    int invokeCLI(
        const std::string &program,
        std::vector<std::string> arguments,
        const std::string &currentDirectory = "") {
        std::vector<char *> cstrings;
        cstrings.reserve(arguments.size());

        for (auto &arg : arguments)
            cstrings.push_back(arg.data());

        cstrings.push_back(nullptr);

        // POSIX/Unix?
        auto process = fork();

        if (!process) {
            if (!currentDirectory.empty())
                chdir(fs::absolute(currentDirectory).string().c_str());

            execvp(program.c_str(), cstrings.data());

            exit(0);
        }

        int status {};
        waitpid(process, &status, 0);
        return status;
    }

    void CLIAddOptions::execute() {
        auto config = ProjectConfig::loadFromThrows(projectFile);

        auto packagesDirectory = fs::path(config.packagesDirectory);

        if (name.empty())
            throw;

        auto package = packagesDirectory / name;

        if (!fs::is_directory(package)) {
            fs::create_directories(package);
        } else {
            logHeader(LogSource::package);
            fmt::print("Removing existing directory ");
            fmt::print(fmt::emphasis::italic, "{}\n", package.string());

            fs::remove_all(package);
        }

        // no more libgit2 for now
        if (invokeCLI("git", { root, "clone", url, package.string() }))
            throw std::runtime_error("Cannot clone git repository.");

        if (fs::exists(package / "project.yaml")) {
            fmt::print("Kara Project Detected");
        } else if (fs::exists(package / "CMakeLists.txt")) {
            auto packageBuild = package / "build"; // conflict :O
            if (!fs::is_directory(packageBuild))
                fs::create_directories(packageBuild);

            std::vector<std::string> arguments = {
                root,
                "-S", fs::absolute(package).string(),
                "-G", "CodeBlocks - Unix Makefiles",
            };

            if (invokeCLI("cmake", arguments, packageBuild.string()))
                throw std::runtime_error("Failed to build CMake project.");

            fs::path cbpPath;

            for (const auto &test : fs::directory_iterator { packageBuild }) {
                if (test.is_directory())
                    continue;

                auto &testPath = test.path();

                if (testPath.extension() == ".cbp") {
                    cbpPath = testPath;
                    break;
                }
            }

            if (cbpPath.empty())
                throw std::runtime_error("No CBP found in directory.");

            pugi::xml_document doc;
            if (!doc.load_file(cbpPath.string().c_str()))
                throw std::runtime_error("Cannot load CBP project file.");

            fmt::print("Returned From Cmake.\n");
        } else {
            throw std::runtime_error("Unknown project type.");
        }
    }
}
