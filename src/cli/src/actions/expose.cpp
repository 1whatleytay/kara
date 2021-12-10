#include <cli/cli.h>

#include <cli/config.h>
#include <cli/manager.h>
#include <cli/exposer.h>

#include <interfaces/interfaces.h>

#include <yaml-cpp/yaml.h>

#include <fmt/format.h>

namespace kara::cli {
    void CLIExposeOptions::execute() {
        try {
            auto config = TargetConfig::loadFrom(projectFile);

            if (!config) {
                auto path = fs::absolute(fs::path(projectFile)).string();

                throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
            }

            ProjectManager manager(*config, "", root);

            auto resolvedTarget = target;
            if (resolvedTarget.empty())
                resolvedTarget = config->resolveName();

            auto targetInfo = manager.readTarget(resolvedTarget);

            auto localType = type;

            if (localType.empty())
                localType = "kara";

            std::transform(
                localType.begin(), localType.end(), localType.begin(),
                [](char c) { return std::tolower(c); });

            if (localType == "c") {
                for (const auto &library : targetInfo.includes) {
                    if (auto path = library.match(filePath)) {
                        std::vector<const char *> arguments = {
                            root.c_str(), path->c_str()
                        };
                        arguments.reserve(library.arguments.size());

                        for (const auto &argument : library.arguments)
                            arguments.push_back(argument.c_str());

                        auto result = kara::interfaces::header::create(
                            static_cast<int>(arguments.size()), arguments.data());

                        YAML::Emitter emitter;

                        emitter << YAML::BeginMap;

                        emitter << YAML::Key << "root" << YAML::Value;
                        expose(std::get<1>(result).get(), emitter);

                        emitter << YAML::EndMap;

                        fmt::print("{}\n", emitter.c_str());

                        return; // dangerous...
                    }
                }

                throw std::runtime_error(fmt::format("No file to match {}.", filePath));
            } else {
                throw std::runtime_error(fmt::format("Unhandled file type {}.", localType));
            }
        } catch (const std::exception &e) {
            YAML::Emitter emitter;

            emitter << YAML::BeginMap;
            emitter << YAML::Key << "error" << YAML::Value << e.what();
            emitter << YAML::EndMap;

            fmt::print("{}\n", emitter.c_str());
        }
    }
}