#include <cli/config.h>

#include <yaml-cpp/yaml.h>

#include <fmt/printf.h>

#include <fstream>

namespace kara::cli {
    TargetConfig::TargetConfig(const YAML::Node &node) {
        if (auto value = node["type"]) {
            std::unordered_map<std::string, TargetType> targetMap = {
                { "library", TargetType::Library },
                { "executable", TargetType::Executable },
                { "interface", TargetType::Interface },
            };

            auto it = targetMap.find(value.as<std::string>());
            if (it == targetMap.end())
                fmt::print("Warning: unknown target type {}.", value.as<std::string>());
            else
                type = it->second;
        }

        if (auto value = node["files"]) {
            if (value.IsSequence()) {
                for (const auto &file : value) {
                    files.insert(file.as<std::string>());
                }
            }
        }

        if (auto value = node["external"]) {
            if (value.IsSequence()) {
                for (const auto &file : value) {
                    external.insert(file.as<std::string>());
                }
            }
        }

        if (auto value = node["libraries"]) {
            if (value.IsSequence()) {
                for (const auto &library : value) {
                    libraries.insert(library.as<std::string>());
                }
            }
        }

        if (auto value = node["linker-options"])
            linkerOptions = value.as<std::vector<std::string>>();

        if (auto value = node["options"]) {
            if (value.IsMap()) {
                if (auto v = value["triple"])
                    defaultOptions.triple = v.as<std::string>();

                if (auto v = value["malloc"])
                    defaultOptions.malloc = v.as<std::string>();
                if (auto v = value["free"])
                    defaultOptions.free = v.as<std::string>();
                if (auto v = value["realloc"])
                    defaultOptions.realloc = v.as<std::string>();

                if (auto v = value["mutable-globals"])
                    defaultOptions.mutableGlobals = v.as<bool>();
            }
        }
    }

    std::optional<ProjectConfig> ProjectConfig::loadFrom(const std::string &path) {
        std::ifstream stream(path);

        if (!stream.is_open())
            return std::nullopt;

        return ProjectConfig(YAML::Load(stream));
    }

    ProjectConfig ProjectConfig::loadFromThrows(const std::string &path) {
        auto config = loadFrom(path);

        if (!config) {
            auto absolute = fs::absolute(fs::path(path)).string();

            throw std::runtime_error(fmt::format("Cannot find config file at {}.", absolute));
        }

        return *config;
    }

    ProjectConfig::ProjectConfig(const YAML::Node &node) {
        if (auto value = node["default"])
            defaultTarget = value.as<std::string>();

        if (auto value = node["output"])
            outputDirectory = value.as<std::string>();

        if (auto value = node["packages"])
            packagesDirectory = value.as<std::string>();

        if (auto value = node["targets"]) {
            if (value.IsMap()) {
                for (const auto &target : value) {
                    targets.insert({ target.first.as<std::string>(), TargetConfig(target.second) });
                }
            }
        }
    }
}
