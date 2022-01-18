#include <cli/packages.h>

#include <cli/log.h>
#include <cli/cbp.h>
#include <cli/config.h>
#include <cli/utility.h>
#include <cli/platform.h>

#include <yaml-cpp/yaml.h>

#include <uriparser/Uri.h>

#include <unistd.h>

#include <fstream>
#include <iostream>

namespace kara::cli {
    PackageBuildResult PackageManager::build(
        const fs::path &config,
        const std::string &name,
        const std::string &suggestTarget,
        const std::vector<std::string> &arguments) {
        if (config.filename() == "CMakeLists.txt") {
            log(LogSource::package, "Building CMake Project {}", config);

            auto packageBuild = packagesDirectory / "cmake" / name / "build"; // conflict :O
            if (!fs::is_directory(packageBuild))
                fs::create_directories(packageBuild);

            auto package = config.parent_path();

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

            auto targetIsValid = [&highlightedTargets, &otherTargets](const std::string &target) {
                return highlightedTargets.find(target) != highlightedTargets.end()
                    || otherTargets.find(target) != otherTargets.end();
            };

            std::string target = suggestTarget;

            if (!targetIsValid(target)) {
                log(LogSource::package, "Select a CMake Target by entering one of the following options.");

                if (!highlightedTargets.empty()) {
                    std::string text = "Main Targets: ";

                    fmt::print("{}", text);

                    bool first = true;

                    // limit line length
                    constexpr size_t maxLength = 40;
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
                    constexpr size_t maxLength = 40;
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
            }

            std::string defaultTarget = "error-target";

            if (!highlightedTargets.empty())
                defaultTarget = *highlightedTargets.begin();
            else if (!otherTargets.empty())
                defaultTarget = *otherTargets.begin();

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
                "--target", targetInfo.name,
            };

            if (!arguments.empty()) {
                buildArguments.emplace_back("--");
                buildArguments.insert(buildArguments.end(), arguments.begin(), arguments.end());
            }

            if (invokeCLI("cmake", buildArguments, packageBuild.string()))
                throw std::runtime_error("Failed to build CMake project.");

            TargetConfig library;

            library.name = target;
            library.type = TargetType::Interface;
            library.options.libraries.emplace_back(targetInfo.output);
            library.options.includeArguments.emplace_back("--");

            for (const auto &include : targetInfo.includes) {
                library.options.includes.emplace_back(include);
                library.options.includeArguments.emplace_back(fmt::format("-I{}", include));
            }

            auto libraryOutput = library.serialize();
            auto libraryOutputFilename = fmt::format("{}.yaml", name);
            auto libraryOutputPath = packagesDirectory / libraryOutputFilename;

            {
                std::ofstream stream(libraryOutputPath);
                if (!stream.is_open())
                    throw std::runtime_error(fmt::format("Cannot write library source file for {}", target));

                stream << libraryOutput;
            }

            log(LogSource::package, "Generated library source file at {}", fs::absolute(libraryOutputPath).string());

//            lockFile.packagesInstalled[url] = { libraryOutputFilename };
//            lockFileWriteStream() << lockFile.serialize();
//
//            logHeader(LogSource::package);
//            fmt::print(fmt::emphasis::bold | fmt::fg(fmt::color::forest_green), "Package {} installed.\n", name);

            return PackageBuildResult { { libraryOutputPath }, { target } };
        } else if (config.extension() == ".yaml") {
            log(LogSource::package, "Detected Kara Project File {}", config);

            throw;
        } else {
            throw std::runtime_error("Unknown project type.");
        }

        throw;
    }

    PackageBuildResult PackageManager::download(
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

                // drop .git
                std::string trail = ".git";

                if (name.size() > trail.size() && name.substr(name.size() - trail.size()) == trail)
                    name = name.substr(0, name.size() - trail.size());
            }

            uriFreeUriMembersA(&uriData);
        }

        if (name.empty())
            throw std::runtime_error("No name found for package.");

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

        PackageBuildResult result;

        if (fs::exists(package / "project.yaml")) {
            result = build(package / "project.yaml", name, suggestTarget, arguments);
        } else if (fs::exists(package / "CMakeLists.txt")) {
            result = build(package / "CMakeLists.txt", name, suggestTarget, arguments);
        } else {
            throw;
        }

        auto &files = lockFile.packagesInstalled[url];
        files.reserve(result.configFiles.size());

        for (const auto &file : result.configFiles)
            files.push_back(fs::relative(file, packagesDirectory));

        lockFileWriteStream() << lockFile.serialize();

        logHeader(LogSource::package);
        fmt::print(fmt::emphasis::bold | fmt::fg(fmt::color::forest_green), "Package {} installed.\n", name);

        return result;
    }

    std::vector<std::string> PackageManager::install(
        const std::string &url,
        const std::string &suggestTarget,
        const std::vector<std::string> &arguments) {
        // could be O(1) but I want to make sure serialization order is consistent
        auto it = lockFile.packagesInstalled.find(url);
        if (it == lockFile.packagesInstalled.end())
            return download(url, suggestTarget, arguments).configFiles;

        std::vector<std::string> output(it->second.size());
        std::transform(it->second.begin(), it->second.end(), output.begin(), [this](const auto &r) {
            return (packagesDirectory / r).string();
        });

        return output;
    }

    std::ofstream PackageManager::lockFileWriteStream() const {
        std::ofstream stream(packagesDirectory / "package-lock.yaml");
        if (!stream.is_open())
            throw std::runtime_error("Failed to open lock file.");

        return stream;
    }

    PackageBuildResult PackageManager::buildCMakePackage(const std::string &name) {
        // cmake --find-package -DNAME=Vulkan -DCOMPILER_ID=GNU -DLANGUAGE=C -DMODE=COMPILE

        const char *cmakeFileContents =
            "# Auto File For CMake Package Loading\n"
            "project(package-loader)\n"
            "\n"
            "cmake_minimum_required(VERSION 3.17)\n";

        auto packageLocation = packagesDirectory / "cmake" / "package_loader";

        auto packageBuild = packageLocation / "build"; // conflict :O
        if (!fs::is_directory(packageBuild))
            fs::create_directories(packageBuild);

        auto cmakeFilePath = packageLocation / "CMakeLists.txt";

        if (!fs::exists(cmakeFilePath)) {
            std::ofstream stream(cmakeFilePath);

            if (!stream.is_open())
                throw std::runtime_error(fmt::format("Cannot open file {} for writing.", cmakeFilePath.string()));

            stream << cmakeFileContents;
        }

        // build temporary cmake project
        // TODO: cache in package lock?
        if (invokeCLI("cmake", { root, fs::absolute(packageLocation).string() }, packageBuild))
            throw std::runtime_error("Failed to run CMake for base project.");

        std::vector<std::string> baseArguments = {
            root,
            "--find-package",
            fmt::format("-DNAME={}", name), // might be able to escape here, i hope not
            "-DCOMPILER_ID=GNU",
            "-DLANGUAGE=C",
        };

        auto baseWith = [&baseArguments](std::string last) {
            auto copy = baseArguments;

            copy.push_back(std::move(last));

            return copy;
        };

        auto [compileStatus, compileBuffer] = invokeCLIWithStdOut("cmake", baseWith("-DMODE=COMPILE"));

        if (compileStatus)
            throw std::runtime_error(fmt::format("Failed to find compile arguments for cmake package {}.", name));

        auto [linkStatus, linkBuffer] = invokeCLIWithStdOut("cmake", baseWith("-DMODE=LINK"));

        if (linkStatus)
            throw std::runtime_error(fmt::format("Failed to find link arguments for cmake package {}.", name));

        std::string compileArguments(compileBuffer.begin(), compileBuffer.end());
        std::string linkArguments(linkBuffer.begin(), linkBuffer.end());

        auto compileArgumentsParsed = platform.parseCLIArgumentsFromString(compileArguments);
        auto linkArgumentsParsed = platform.parseCLIArgumentsFromString(linkArguments);

        std::vector<std::string> linkArgumentsCleaned;
        for (const auto &argument : linkArgumentsParsed) {
            auto newArguments = platform.parseLinkerDriverArgument(argument);

            linkArgumentsCleaned.insert(linkArgumentsCleaned.end(), newArguments.begin(), newArguments.end());
        }

        TargetConfig library;

        library.name = name;
        library.type = TargetType::Interface;
        // libraries unused?
        library.options.linkerOptions = linkArgumentsCleaned;
        library.options.includeArguments.emplace_back("--");
        library.options.includeArguments.insert(library.options.includeArguments.end(),
            compileArgumentsParsed.begin(), compileArgumentsParsed.end());

        for (const auto &argument : compileArgumentsParsed) {
            auto dirs = platform.parseDirectoriesFromCompileArgument(argument);

            library.options.includes.insert(library.options.includes.end(),
                dirs.begin(), dirs.end());
        }

        auto libraryOutput = library.serialize();
        auto libraryOutputFilename = fmt::format("{}.yaml", name);
        auto libraryOutputPath = packagesDirectory / libraryOutputFilename;

        {
            std::ofstream stream(libraryOutputPath);
            if (!stream.is_open())
                throw std::runtime_error(fmt::format("Cannot write library source file for cmake package {}", name));

            stream << libraryOutput;
        }

        log(LogSource::package, "Generated library source file at {}", fs::absolute(libraryOutputPath).string());

        return PackageBuildResult { { libraryOutputPath }, { name } };
    }

    PackageManager::PackageManager(Platform &platform, fs::path packagesDirectory, std::string root)
        : platform(platform), packagesDirectory(std::move(packagesDirectory)), root(std::move(root)) {
        if (!fs::exists(this->packagesDirectory))
            fs::create_directories(this->packagesDirectory);

        auto lockFilePath = this->packagesDirectory / "package-lock.yaml";

        if (fs::exists(lockFilePath))
            lockFile = PackageLockFile(YAML::LoadFile(lockFilePath.string()));
    }
}
