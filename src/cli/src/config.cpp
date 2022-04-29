#include <cli/config.h>

#include <yaml-cpp/yaml.h>

#include <fmt/printf.h>

#include <uriparser/Uri.h>

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    namespace {
        bool isHttpOrHttps(const char *text) {
            UriUriA uri;

            // not a url
            if (uriParseSingleUriA(&uri, text, nullptr))
                return false;

            auto exit = [&uri](bool value) {
                uriFreeUriMembersA(&uri);

                return value;
            };

            // exit { uri.uriFreeUriMembersA }

            auto size = uri.scheme.afterLast - uri.scheme.first;

            if (size >= 5 && memcmp(uri.scheme.first, "https", 5) == 0)
                return exit(true);

            if (size >= 4 && memcmp(uri.scheme.first, "http", 4) == 0)
                return exit(true);

            return exit(false);
        }
    }

    bool TargetOptions::operator==(const TargetOptions &other) const {
        return includes == other.includes && includeArguments == other.includeArguments && libraries == other.libraries
            && dynamicLibraries == other.dynamicLibraries && linkerOptions == other.linkerOptions
            && defaultOptions == other.defaultOptions;
    }

    bool TargetOptions::operator!=(const TargetOptions &other) const { return !operator==(other); }

    void TargetOptions::merge(const TargetOptions &other) {
        if (!other.includes.empty()) {
            includes.insert(includes.end(), other.includes.begin(), other.includes.end());
        }

        if (!other.includeArguments.empty()) {
            includeArguments.insert(
                includeArguments.end(), other.includeArguments.begin(), other.includeArguments.end());
        }

        if (!other.libraries.empty()) {
            libraries.insert(libraries.end(), other.libraries.begin(), other.libraries.end());
        }

        if (!other.dynamicLibraries.empty()) {
            dynamicLibraries.insert(
                dynamicLibraries.end(), other.dynamicLibraries.begin(), other.dynamicLibraries.end());
        }

        if (!other.linkerOptions.empty()) {
            linkerOptions.insert(linkerOptions.end(), other.linkerOptions.begin(), other.linkerOptions.end());
        }

        defaultOptions.merge(other.defaultOptions);
    }

    void TargetOptions::serializeInline(YAML::Emitter &emitter) const {
        if (!includes.empty())
            emitter << YAML::Key << "includes" << YAML::Value << includes;

        if (!includeArguments.empty())
            emitter << YAML::Key << "include-arguments" << YAML::Value << includeArguments;

        if (!libraries.empty())
            emitter << YAML::Key << "libraries" << YAML::Value << libraries;

        if (!dynamicLibraries.empty())
            emitter << YAML::Key << "dynamic-libraries" << YAML::Value << dynamicLibraries;

        if (!linkerOptions.empty())
            emitter << YAML::Key << "linker-options" << YAML::Value << linkerOptions;

        bool changed = false;
        YAML::Node options; // ...

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

        if (defaultOptions.rawPlatform)
            pushOptions("raw-platform", defaultOptions.rawPlatform);
        if (defaultOptions.mutableGlobals)
            pushOptions("mutable-globals", defaultOptions.mutableGlobals);

        if (changed)
            emitter << YAML::Key << "options" << YAML::Value << options;
    }

    void TargetOptions::serialize(YAML::Emitter &emitter) const {
        emitter << YAML::BeginMap;

        serializeInline(emitter);

        emitter << YAML::EndMap;
    }

    TargetOptions::TargetOptions(const YAML::Node &node) {
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

        if (auto value = node["options"]) {
            if (auto v = value["triple"])
                defaultOptions.triple = v.as<std::string>();

            if (auto v = value["malloc"])
                defaultOptions.malloc = v.as<std::string>();
            if (auto v = value["free"])
                defaultOptions.free = v.as<std::string>();
            if (auto v = value["realloc"])
                defaultOptions.realloc = v.as<std::string>();

            if (auto v = value["raw-platform"])
                defaultOptions.rawPlatform = v.as<bool>();
            if (auto v = value["mutable-globals"])
                defaultOptions.mutableGlobals = v.as<bool>();
        }
    }

    TargetImportKind TargetImport::detectedKind() const {
        if (kind)
            return *kind;

        if (isHttpOrHttps(path.c_str()))
            return TargetImportKind::RepositoryUrl;
        else
            return TargetImportKind::ProjectFile;
    }

    void TargetImport::serialize(YAML::Emitter &emitter) const {
        if (kind) {
            emitter << YAML::BeginMap;

            auto nameFromKind = [](TargetImportKind k) {
                switch (k) {
                case ProjectFile:
                    return "file";
                case RepositoryUrl:
                    return "url";
                case CMakePackage:
                    return "cmake";
                default:
                    throw std::runtime_error("Unimplemented target import kind.");
                }
            };

            emitter << YAML::Key << nameFromKind(*kind) << YAML::Value << path;

            if (!targets.empty())
                emitter << YAML::Key << "targets" << YAML::Value << targets;

            if (!buildArguments.empty())
                emitter << YAML::Key << "build-arguments" << YAML::Value << buildArguments;

            options.serializeInline(emitter);

            emitter << YAML::EndMap;
        } else {
            emitter << path;
        }
    }

    TargetImport::TargetImport(const YAML::Node &node) {
        if (node.IsMap()) {
            auto setPath = [this](YAML::Node &node, TargetImportKind specifiedKind) {
                path = node.as<std::string>();
                kind = specifiedKind;
            };

            if (auto fileValue = node["file"])
                setPath(fileValue, TargetImportKind::ProjectFile);
            else if (auto urlValue = node["url"])
                setPath(urlValue, TargetImportKind::RepositoryUrl);
            else if (auto cmakeValue = node["cmake"])
                setPath(cmakeValue, TargetImportKind::CMakePackage);
            else
                throw std::runtime_error("Need one of `file` or `url` to be specified for config imports.");

            if (auto value = node["targets"])
                targets = value.as<std::vector<std::string>>();

            if (auto value = node["build-arguments"])
                buildArguments = value.as<std::vector<std::string>>();

            options = TargetOptions(node);
        } else {
            // auto detect target type
            path = node.as<std::string>();
        }
    }

    std::string TargetConfig::resolveName() const {
        if (!name.empty())
            return name;

        fs::path rootPath(root);

        if (rootPath.has_stem())
            return rootPath.stem();

        throw std::runtime_error(fmt::format("Could not resolve target name for {}.", root));
    }

    // assumption: resolves exactly the targets that it depends on
    // later: there should be some common store for targets
    //    void TargetConfig::resolveConfigs(std::unordered_map<std::string, const TargetConfig *> &values) const {
    //        auto evaluatedName = resolveName();
    //
    //        if (!values.insert({ evaluatedName, this }).second)
    //            throw std::runtime_error(fmt::format("Multiple configs found with name {}.", evaluatedName));
    //
    //        throw; // i dont want to think about this right now
    ////        for (const auto &config : configs)
    ////            config.resolveConfigs(values);
    //    }

    //    std::unordered_map<std::string, const TargetConfig *> TargetConfig::resolveConfigs() const {
    //        std::unordered_map<std::string, const TargetConfig *> result;
    //        resolveConfigs(result);
    //
    //        return result;
    //    }

    std::string TargetConfig::serialize() const {
        YAML::Emitter emitterBase;

        emitterBase << YAML::BeginMap;

        auto typeName = [this]() {
            switch (type) {
            case TargetType::Library:
                return "library";
            case TargetType::Executable:
                return "executable";
            case TargetType::Interface:
                return "interface";
            default:
                throw;
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
            emitter() << YAML::Value << YAML::BeginSeq;

            for (const auto &e : import) {
                e.serialize(emitter());
            }

            emitter() << YAML::EndSeq;
        }

        block();

        options.serializeInline(emitter());

        //        if (!packages.empty()) {
        //            emitter() << YAML::Key << "packages";
        //            emitter() << YAML::BeginMap;
        //            for (const auto &pair : packages) {
        //                emitter() << YAML::Key << pair.first;
        //                emitter() << YAML::Value << YAML::Flow << pair.second;
        //            }
        //            emitter() << YAML::EndMap;
        //        }

        emitterBase << YAML::EndMap;

        return emitterBase.c_str();
    }

    TargetConfig::TargetConfig(std::string root, const YAML::Node &node)
        : root(std::move(root)) {
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

        if (auto value = node["files"]) {
            auto values = value.as<std::vector<std::string>>();

            files = std::set<std::string> { values.begin(), values.end() };
        }

        if (auto value = node["output-directory"])
            outputDirectory = value.as<std::string>();
        if (auto value = node["packages-directory"])
            packagesDirectory = value.as<std::string>();

        if (auto value = node["import"]) {
            for (const auto &decl : value) {
                import.emplace_back(decl);
            }
        }

        options = TargetOptions(node);
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
