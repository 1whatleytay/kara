#pragma once

#include <builder/library.h>

#include <parser/root.h>

#include <options/options.h>

#include <filesystem>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace kara::builder {
    struct SourceManager;

    struct SourceFile {
        fs::path path;

        std::string type;

        std::unique_ptr<hermes::State> state;
        std::unique_ptr<parser::Root> root;

        std::set<std::tuple<fs::path, std::string>> dependencies;

        SourceFile(fs::path path, std::string type, const Library *library = nullptr);
    };

    using SourceDatabaseCallback = std::function<void(const fs::path &path, const std::string &type)>;

    struct SourceDatabase {
        SourceDatabaseCallback callback;

        std::unordered_map<std::string, std::unique_ptr<SourceFile>> nodes;

        const SourceFile &get(const fs::path &absolute, const std::string &type = "", const Library *library = nullptr);

        explicit SourceDatabase(SourceDatabaseCallback callback = SourceDatabaseCallback());
    };

    struct SourceManager {
        SourceDatabase &database;

        std::vector<Library> libraries;

        void resolve(const SourceFile &file, std::unordered_set<const SourceFile *> &visited);
        [[nodiscard]] std::unordered_set<const SourceFile *> resolve(const SourceFile &file);

        const SourceFile &get(const fs::path &path, const fs::path &root = "", const std::string &type = "");

        SourceManager(SourceDatabase &database, std::vector<Library> libraries);
    };
}
