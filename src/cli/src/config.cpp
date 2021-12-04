#include <cli/config.h>

#include <yaml-cpp/yaml.h>

#include <fmt/printf.h>

#include <fstream>

namespace kara::cli {
    std::set<std::string> getStringSet(const YAML::Node &node) {
        auto values = node.as<std::vector<std::string>>();

        return { values.begin(), values.end() };
    }

    std::string TargetConfig::resolveName() const {
        if (!name.empty())
            return name;

        if (root.has_stem())
            return root.stem();

        throw std::runtime_error(fmt::format("Could not resolve target name for {}.", root.string()));
    }

    void TargetConfig::resolveConfigs(std::unordered_map<std::string, const TargetConfig *> &values) const {
        auto evaluatedName = resolveName();

        if (!values.insert({ evaluatedName, this }).second)
            throw std::runtime_error(fmt::format("Multiple configs found with name {}.", evaluatedName));

        for (const auto &config : configs)
            config.resolveConfigs(values);
    }

    std::unordered_map<std::string, const TargetConfig *> TargetConfig::resolveConfigs() const {
        std::unordered_map<std::string, const TargetConfig *> result;
        resolveConfigs(result);

        return result;
    }

    std::string TargetConfig::serialize() const {
        YAML::Emitter emitterBase;

        emitterBase << YAML::BeginMap;

        auto typeName = [this]() {
            switch (type) {
            case TargetType::Library: return "library";
            case TargetType::Executable: return "executable";
            case TargetType::Interface: return "interface";
            }
        };

        bool blockEmpty = true;
        auto block = [&blockEmpty, &emitterBase]() {
            if (!blockEmpty) {
                emitterBase << YAML::Newline << YAML::Newline;
                blockEmpty = true;
            }
        };

        auto emitter = [&blockEmpty, &emitterBase]() -> YAML::Emitter & {
            blockEmpty = false;
            return emitterBase;
        };

        emitter() << YAML::Key << "type" << YAML::Value << typeName();
        block();

        if (!name.empty())
            emitter() << YAML::Key << "name" << YAML::Value << name;

        block();

        if (!files.empty()) {
            emitter() << YAML::Key << "files";
            emitter() << YAML::Value << std::vector<std::string>(files.begin(), files.end()); // copy
        }

        block();

        if (outputDirectory != "build") // default, this is awful...
            emitter() << YAML::Key << "output-directory" << YAML::Value << outputDirectory;
        if (packagesDirectory != "build") // default, this is awful...
            emitter() << YAML::Key << "packages-directory" << YAML::Value << packagesDirectory;

        block();

        if (!import.empty()) {
            emitter() << YAML::Key << "import";
            emitter() << YAML::Value << std::vector<std::string>(import.begin(), import.end()); // copy
        }

        block();

        if (!includes.empty())
            emitter() << YAML::Key << "includes" << YAML::Value << includes;
        block();

        if (!includeArguments.empty())
            emitter() << YAML::Key << "include-arguments" << YAML::Value << includeArguments;
        block();

        if (!libraries.empty())
            emitter() << YAML::Key << "libraries" << YAML::Value << libraries;
        block();

        if (!dynamicLibraries.empty())
            emitter() << YAML::Key << "dynamic-libraries" << YAML::Value << dynamicLibraries;
        block();

        if (!linkerOptions.empty())
            emitter() << YAML::Key << "linker-options" << YAML::Value << linkerOptions;
        block();

        if (!packages.empty()) {
            emitter() << YAML::Key << "packages";
            emitter() << YAML::BeginMap;
            for (const auto &pair : packages) {
                emitter() << YAML::Key << pair.first;
                emitter() << YAML::Value << YAML::Flow << pair.second;
            }
            emitter() << YAML::EndMap;
        }

        block();

        bool changed = false;
        YAML::Node options;

        auto pushOptions = [&options, &changed](const std::string &key, const auto &value) {
            changed = true;
            options[key] = value;
        };

        if (!defaultOptions.triple.empty())
            pushOptions("triple", defaultOptions.triple);

        if (defaultOptions.malloc != "malloc")
            pushOptions("malloc", defaultOptions.malloc);
        if (defaultOptions.free != "free")
            pushOptions("free", defaultOptions.free);
        if (defaultOptions.realloc != "realloc")
            pushOptions("realloc", defaultOptions.realloc);

        if (defaultOptions.mutableGlobals)
            pushOptions("mutable-globals", defaultOptions.mutableGlobals);

        if (changed)
            emitter() << YAML::Key << "options" << YAML::Value << options;

        block();

        emitterBase << YAML::EndMap;

        return emitterBase.c_str();
    }

    TargetConfig::TargetConfig(fs::path root, const YAML::Node &node) : root(std::move(root)) {
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

        if (auto value = node["name"])
            name = value.as<std::string>();

        if (auto value = node["files"])
            files = getStringSet(value);

        if (auto value = node["output-directory"])
            outputDirectory = value.as<std::string>();
        if (auto value = node["packages-directory"])
            packagesDirectory = value.as<std::string>();

        if (auto value = node["import"])
            import = getStringSet(value);

        if (auto value = node["includes"])
            includes = value.as<std::vector<std::string>>();
        if (auto value = node["include-arguments"])
            includeArguments = value.as<std::vector<std::string>>();
        if (auto value = node["libraries"])
            libraries = value.as<std::vector<std::string>>();
        if (auto value = node["dynamic-libraries"])
            dynamicLibraries = value.as<std::vector<std::string>>();
        if (auto value = node["linker-options"])
            linkerOptions = value.as<std::vector<std::string>>();

        if (auto value = node["packages"]) {
            for (const auto &pair : value) {
                auto path = pair.first.as<std::string>();
                auto toImport = pair.second.as<std::vector<std::string>>();

                packages[path] = toImport;
            }
        }

        if (auto value = node["options"]) {
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

        for (const auto &part : import) {
            auto path = fs::path(part);
            auto newPath = path.is_absolute() ? path : this->root.parent_path() / path;

            configs.push_back(TargetConfig::loadFromThrows(newPath.string()));
        }
    }

    std::optional<TargetConfig> TargetConfig::loadFrom(const std::string &path) {
        std::ifstream stream(path);

        if (!stream.is_open())
            return std::nullopt;

        return TargetConfig(fs::path(path), YAML::Load(stream));
    }

    TargetConfig TargetConfig::loadFromThrows(const std::string &path) {
        auto config = loadFrom(path);

        if (!config) {
            auto absolute = fs::absolute(fs::path(path)).string();

            throw std::runtime_error(fmt::format("Cannot find config file at {}.", absolute));
        }

        return *config;
    }
}
