#include <builder/library.h>

#include <yaml-cpp/yaml.h>

namespace kara::builder {
    std::string LibraryDocument::serialize() const {
        YAML::Emitter emitter;

        emitter << YAML::BeginMap;

        emitter << YAML::Key << "language";
        emitter << YAML::Value << language;

        // This will probably be a problem in MSVC builds?
        // unless YAML cpp is actually aware of std::filesystem, impressive!!
        emitter << YAML::Key << "includes";
        emitter << YAML::Value << includes;

        emitter << YAML::Key << "libraries";
        emitter << YAML::Value << libraries;

        emitter << YAML::Key << "dynamic-libraries";
        emitter << YAML::Value << dynamicLibraries;

        emitter << YAML::Key << "arguments";
        emitter << YAML::Value << arguments;

        emitter << YAML::EndMap;

        return emitter.c_str();
    }

    std::optional<std::string> LibraryDocument::match(const std::string &header) const {
        for (const auto &include : includes) {
            fs::path test = include / header;

            if (fs::exists(test)) {
                return test;
            }
        }

        return std::nullopt;
    }

    LibraryDocument::LibraryDocument(const std::string &text, const fs::path &root) {
        auto doc = YAML::Load(text);

        language = doc["language"].as<std::string>();

        assert(language == "c");

        for (const auto &i : doc["includes"]) {
            fs::path k = i.as<std::string>();

            if (k.is_absolute())
                includes.push_back(k);
            else
                includes.push_back(root / k);
        }

        for (const auto &i : doc["libraries"]) {
            fs::path k = i.as<std::string>();

            if (k.is_absolute())
                libraries.push_back(k);
            else
                libraries.push_back(root / k);
        }

        for (const auto &i : doc["dynamic-libraries"]) {
            fs::path k = i.as<std::string>();

            if (k.is_absolute())
                dynamicLibraries.push_back(k);
            else
                dynamicLibraries.push_back(root / k);
        }

        for (const auto &k : doc["arguments"])
            arguments.emplace_back(k.as<std::string>());
    }
}