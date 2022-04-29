#pragma once

#include <builder/library.h>

#include <parser/root.h>

#include <options/options.h>

#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace kara::builder {
    struct SourceManager;

    struct SourceFile {
        std::string path;

        std::string type;

        std::unique_ptr<hermes::State> state;
        std::unique_ptr<parser::Root> root;

        std::set<std::tuple<std::string, std::string>> dependencies;

        SourceFile(std::string path, std::string type, const Library *library = nullptr);
    };

    using SourceDatabaseCallback = std::function<void(const std::string &path, const std::string &type)>;

    struct SourceDatabase {
        SourceDatabaseCallback callback;

        std::unordered_map<std::string, std::unique_ptr<SourceFile>> nodes;

        const SourceFile &get(const std::string &absolute, const std::string &type = "", const Library *library = nullptr);

        explicit SourceDatabase(SourceDatabaseCallback callback = SourceDatabaseCallback());
    };

    struct SourceManager {
        SourceDatabase &database;

        std::vector<Library> libraries;

        void resolve(const SourceFile &file, std::unordered_set<const SourceFile *> &visited);
        [[nodiscard]] std::unordered_set<const SourceFile *> resolve(const SourceFile &file);

        const SourceFile &get(const std::string &path, const std::string &root = "", const std::string &type = "");

        SourceManager(SourceDatabase &database, std::vector<Library> libraries);
    };
}
