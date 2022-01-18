#include <cli/lock.h>

#include <cli/utility.h>

#include <yaml-cpp/yaml.h>

namespace kara::cli {
    std::string PackageLockFile::createPath(const std::string &parent) {
        fs::path parentPath(parent);

        if (!fs::exists(parentPath))
            fs::create_directories(parentPath);

        return (parentPath / "package-lock.yaml");
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

    [[nodiscard]] std::string BuildLockFile::serialize() const {
        YAML::Emitter emitter;

        emitter << YAML::BeginMap;
        if (!parameters.empty()) {

            emitter << YAML::Key << "parameters";
            emitter << YAML::BeginMap;
            for (const auto &package : parameters) {
                emitter << YAML::Key << package.first;
                emitter << YAML::Value << package.second;
            }
            emitter << YAML::EndMap;
        }
        emitter << YAML::EndMap;

        return emitter.c_str();
    }

    std::string BuildLockFile::createPath(const std::string &parent) {
        fs::path parentPath(parent);

        if (!fs::exists(parentPath))
            fs::create_directories(parentPath);

        return (parentPath / "build-lock.yaml");
    }

    BuildLockFile::BuildLockFile(const YAML::Node &node) {
        if (auto value = node["parameters"]) {
            for (const auto &pair : value) {
                auto key = pair.first.as<std::string>();
                auto object = pair.second.as<std::string>();

                parameters.insert({ std::move(key), std::move(object) });
            }
        }
    }
}