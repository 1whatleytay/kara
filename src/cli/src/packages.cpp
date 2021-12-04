#include <cli/packages.h>

#include <cli/log.h>
#include <cli/cbp.h>

#include <yaml-cpp/yaml.h>

#include <uriparser/Uri.h>

#include <unistd.h>

#include <fstream>
#include <iostream>

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

            exit(1);
        }

        int status {};
        waitpid(process, &status, 0);
        return status;
    }

    std::string PackageLockFile::serialize() const {
        YAML::Emitter emitter;

        emitter << YAML::BeginMap;
        emitter << YAML::Key << "packages-installed";
        emitter << YAML::Value;
        emitter << YAML::BeginMap;
        for (const auto &package : packagesInstalled) {
            emitter << YAML::Key << package.first;
            emitter << YAML::Value << package.second;
        }
        emitter << YAML::EndMap;
        emitter << YAML::EndMap;

        return emitter.c_str();
    }

    PackageLockFile::PackageLockFile(const YAML::Node &node) {
        if (auto value = node["packages-installed"]) {
            for (const auto &pair : value) {
                auto package = pair.first.as<std::string>();
                auto files = pair.second.as<std::vector<std::string>>();

                packagesInstalled.insert({ std::move(package), std::move(files) });
            }
        }
    }

    PackageDownloadResult PackageManager::download(
        const std::string &url,
        const std::string &suggestTarget,
        const std::vector<std::string> &arguments) {
        std::string name = url;

        UriUriA uriData;

        if (!uriParseSingleUriA(&uriData, url.c_str(), nullptr)) {
            if (uriData.pathTail) {
                name = std::string(
                    uriData.pathTail->text.first,
                    uriData.pathTail->text.afterLast);
            }

            uriFreeUriMembersA(&uriData);
        }

        if (name.empty())
            throw std::runtime_error("No name found for package. Please specify a name with --name.");

        auto package = packagesDirectory / name;

        if (!fs::is_directory(package)) {
            fs::create_directories(package);
        } else {
            logHeader(LogSource::package);
            fmt::print("Removing existing directory ");
            fmt::print(fmt::emphasis::italic, "{}\n", package.string());

            fs::remove_all(package);
        }

        log(LogSource::package, "Cloning repository {}", url);

        // no more lib git2 for now
        if (invokeCLI("git", { root, "clone", url, package.string(), "--quiet", "--depth", "1" }))
            throw std::runtime_error("Cannot clone git repository.");

        if (fs::exists(package / "project.yaml")) {
            log(LogSource::package, "Detected Kara Project File {}", url);

            throw;
        } else if (fs::exists(package / "CMakeLists.txt")) {
            log(LogSource::package, "Building CMake Project {}", url);

            auto packageBuild = package / "build"; // conflict :O
            if (!fs::is_directory(packageBuild))
                fs::create_directories(packageBuild);

            std::vector<std::string> cmakeArguments = {
                root,
                fs::absolute(package).string(),
                "-G", "CodeBlocks - Unix Makefiles",
            };

            if (invokeCLI("cmake", cmakeArguments, packageBuild.string()))
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

            auto cbp = CBPProject::loadFromThrows(cbpPath.string());

            struct StringSizeOrdering {
                bool operator()(const std::string &left, const std::string &right) const {
                    return left.size() < right.size() || (left.size() == right.size() && left < right);
                }
            };

            std::set<std::string, StringSizeOrdering> highlightedTargets;
            std::set<std::string, StringSizeOrdering> otherTargets;

            for (const auto &target : cbp.targets) {
                if (target.fast)
                    continue;

                if (cbpTargetNameIsSimilar(target.name, cbp.name))
                    highlightedTargets.insert(target.name);
                else
                    otherTargets.insert(target.name);
            }

            assert(!highlightedTargets.empty() || !otherTargets.empty());

            log(LogSource::package, "Select a CMake Target by entering one of the following options.");

            if (!highlightedTargets.empty()) {
                std::string text = "Main Targets: ";

                fmt::print("{}", text);

                bool first = true;

                // limit line length
                constexpr size_t maxLength = 60;
                size_t lineLength = 0;

                for (const auto &targetName : highlightedTargets) {
                    if (lineLength > maxLength) {
                        lineLength = 0;
                        fmt::print("\n{}", std::string(text.size(), ' '));
                        first = true;
                    }

                    if (first) {
                        first = false;
                    } else {
                        fmt::print(", ");
                        lineLength += 2;
                    }


                    fmt::print(fmt::fg(fmt::color::teal) | fmt::emphasis::bold, "{}", targetName);
                    lineLength += targetName.size();
                }

                fmt::print("\n");
            }

            if (!otherTargets.empty()) {
                std::string text = "Other Targets: ";

                fmt::print("{}", text);

                bool first = true;

                // limit line length
                constexpr size_t maxLength = 60;
                size_t lineLength = 0;

                for (const auto &targetName : otherTargets) {
                    if (lineLength > maxLength) {
                        lineLength = 0;
                        fmt::print("\n{}", std::string(text.size(), ' '));
                        first = true;
                    }

                    if (first) {
                        first = false;
                    } else {
                        fmt::print(", ");
                        lineLength += 2;
                    }

                    fmt::print(fmt::fg(fmt::color::orange), "{}", targetName);
                    lineLength += targetName.size();
                }

                fmt::print("\n");
            }

            std::string defaultTarget = "error-target";

            if (!highlightedTargets.empty())
                defaultTarget = *highlightedTargets.begin();
            else if (!otherTargets.empty())
                defaultTarget = *otherTargets.begin();

            auto targetIsValid = [&highlightedTargets, &otherTargets](const std::string &target) {
                return highlightedTargets.find(target) != highlightedTargets.end()
                    || otherTargets.find(target) != otherTargets.end();
            };

            std::string target = suggestTarget;

            while (!targetIsValid(target)) {
                fmt::print("Enter a Target Name (default={}): ", defaultTarget);

                std::getline(std::cin, target);

                if (target.empty())
                    target = defaultTarget;
            }

            auto it = std::find_if(cbp.targets.begin(), cbp.targets.end(), [&target](const CBPTarget &t) {
                return t.name == target && !t.fast;
            });

            if (it == cbp.targets.end())
                throw std::runtime_error(fmt::format("Cannot find target named {}\n", target));

            CBPTarget targetInfo = *it;

            if (targetInfo.output.empty())
                throw std::runtime_error(fmt::format("Target {} does not have any output", target));

            auto commandIt = targetInfo.commands.find("Build");
            if (commandIt == targetInfo.commands.end())
                throw std::runtime_error(fmt::format("Target {} does not have any compile command", target));

            auto command = commandIt->second;

            log(LogSource::package, "Building CMake Target {}", target);

            std::vector<std::string> buildArguments = {
                root,
                "--build", ".",
                "--target", targetInfo.name
            };

            if (!arguments.empty()) {
                buildArguments.emplace_back("--");
                buildArguments.insert(buildArguments.end(), arguments.begin(), arguments.end());
            }

            if (invokeCLI("cmake", buildArguments, packageBuild.string()))
                throw std::runtime_error("Failed to build CMake project.");

            TargetConfig library;

            library.libraries.emplace_back(targetInfo.output);

            library.includeArguments.emplace_back("--");

            for (const auto &include : targetInfo.includes) {
                library.includes.emplace_back(include);
                library.includeArguments.emplace_back(fmt::format("-I{}", include));
            }

            auto libraryOutput = library.serialize();
            auto libraryOutputPath = packagesDirectory / fmt::format("{}.yaml", name);

            {
                std::ofstream stream(libraryOutputPath);
                if (!stream.is_open())
                    throw std::runtime_error(fmt::format("Cannot write library source file for {}", target));

                stream << libraryOutput;
            }

            log(LogSource::package, "Generated library source file at {}", fs::absolute(libraryOutputPath).string());

            lockFile.packagesInstalled[url] = { libraryOutputPath };
            lockFileWriteStream() << lockFile.serialize();

            return PackageDownloadResult { { libraryOutputPath }, { target } };
        } else {
            throw std::runtime_error("Unknown project type.");
        }

        throw; // :flushed:
    }

    std::vector<std::string> PackageManager::install(
        const std::string &url,
        const std::string &suggestTarget,
        const std::vector<std::string> &arguments) {
        // could be O(1) but I want to make sure serialization order is consistent
        auto it = lockFile.packagesInstalled.find(url);
        if (it == lockFile.packagesInstalled.end())
            return download(url, suggestTarget, arguments).configFiles;

        return it->second;
    }


    std::ofstream PackageManager::lockFileWriteStream() const {
        std::ofstream stream(packagesDirectory / "package-lock.yaml");
        if (!stream.is_open())
            throw std::runtime_error("Failed to open lock file.");

        return stream;
    }

    PackageManager::PackageManager(fs::path packagesDirectory, std::string root)
        : packagesDirectory(std::move(packagesDirectory)), root(std::move(root)) {
        if (!fs::exists(this->packagesDirectory))
            fs::create_directories(this->packagesDirectory);

        auto lockFilePath = this->packagesDirectory / "package-lock.yaml";

        if (fs::exists(lockFilePath))
            lockFile = PackageLockFile(YAML::LoadFile(lockFilePath.string()));
    }
}