#include <builder/manager.h>

#include <builder/error.h>

#include <parser/import.h>
#include <parser/literals.h>

#include <interfaces/interfaces.h>

#include <llvm/Support/Host.h>
#include <llvm/Target/TargetOptions.h>

#include <cassert>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::builder {
    namespace files {
        using SourceData = std::pair<std::unique_ptr<hermes::State>, std::unique_ptr<parser::Root>>;

        SourceData makeKara(const fs::path &path) {
            std::stringstream buffer;

            {
                std::ifstream stream(path);
                buffer << stream.rdbuf();
            }

            auto state = std::make_unique<hermes::State>(buffer.str());

            try {
                auto root = std::make_unique<parser::Root>(*state);

                return { std::move(state), std::move(root) };
            } catch (const hermes::ParseError &error) {
                hermes::LineDetails details(state->text, error.index);

                fmt::print("{} [line {}]\n{}\n{}\n", error.issue, details.lineNumber, details.line, details.marker);

                throw;
            }
        }

        SourceData makeC(const fs::path &path, const Library &library) {
            auto thisPath = fs::current_path();
            auto fullPath = fs::absolute(path);

            // TODO: thisPath should probably be executable or root?
            std::vector<const char *> arguments = { thisPath.c_str(), fullPath.c_str() };
            arguments.reserve(arguments.size() + library.arguments.size());

            auto cString = [](const auto &s) { return s.c_str(); };
            std::transform(library.arguments.begin(), library.arguments.end(), std::back_inserter(arguments), cString);

            auto [tupleState, tupleRoot]
                = interfaces::header::create(static_cast<int>(arguments.size()), arguments.data());

            auto state = std::move(tupleState);
            auto root = std::move(tupleRoot);

            return { std::move(state), std::move(root) };
        }
    }

    SourceFile::SourceFile(std::string path, std::string type, const Library *library)
        : path(std::move(path))
        , type(std::move(type)) {
        if (this->type.empty() || this->type == "kara") {
            auto [dataState, dataRoot] = files::makeKara(this->path);

            state = std::move(dataState);
            root = std::move(dataRoot);
        } else if (this->type == "c") {
            assert(library);
            auto [dataState, dataRoot] = files::makeC(this->path, *library);

            state = std::move(dataState);
            root = std::move(dataRoot);
        } else {
            throw;
        }

        for (const auto &e : root->children) {
            if (!e->is(parser::Kind::Import))
                continue;

            auto import = e->as<parser::Import>();
            auto *string = import->body();

            if (!string->inserts.empty())
                throw VerifyError(string, "Imported string node cannot have any inserts.");

            dependencies.insert({ string->text, import->type });
        }
    }

    const SourceFile &SourceDatabase::get(const std::string &absolute, const std::string &type, const Library *library) {
        auto iterator = nodes.find(absolute);
        if (iterator != nodes.end())
            return *iterator->second;

        if (callback)
            callback(absolute, type);

        auto file = std::make_unique<SourceFile>(absolute, type, library);
        auto *ref = file.get();
        nodes[absolute] = std::move(file);

        return *ref;
    }

    SourceDatabase::SourceDatabase(SourceDatabaseCallback callback)
        : callback(std::move(callback)) { }

    // NOLINT(misc-no-recursion)
    void SourceManager::resolve(const SourceFile &file, std::unordered_set<const SourceFile *> &visited) {
        visited.insert(&file);

        for (const auto &[depPath, depType] : file.dependencies) {
            auto &f = get(depPath, fs::path(file.path).parent_path(), depType);

            if (visited.find(&f) != visited.end())
                continue;

            resolve(f, visited);
        }
    }

    std::unordered_set<const SourceFile *> SourceManager::resolve(const SourceFile &file) {
        std::unordered_set<const SourceFile *> result;
        resolve(file, result);

        return result;
    }

    const SourceFile &SourceManager::get(const std::string &path, const std::string &root, const std::string &type) {
        fs::path fsPath(path);

        fs::path fullPath = fsPath.is_absolute() ? fsPath : fs::path(root) / fsPath;
        std::string absoluteName = fs::absolute(fullPath).string();

        const Library *doc = nullptr;

        if (!fs::exists(fullPath)) {
            bool matches = false;

            if (fsPath.is_relative()) {
                for (const auto &library : libraries) {
                    std::optional<std::string> subPath = library.match(path);

                    if (subPath) {
                        fullPath = *subPath;
                        doc = &library;
                        matches = true;

                        break;
                    }
                }
            }

            if (!matches)
                throw std::runtime_error(fmt::format("Cannot find file under path {}.", path));
        }

        return database.get(fullPath, type, doc);
    }

    SourceManager::SourceManager(SourceDatabase &database, std::vector<Library> libraries)
        : database(database)
        , libraries(std::move(libraries)) { }
}
