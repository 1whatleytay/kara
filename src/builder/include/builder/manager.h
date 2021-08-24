#pragma once

#include <parser/root.h>

#include <options/options.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>

#include <set>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace kara::builder {
    struct Builder;
    struct Manager;

    struct LibraryDocument {
        std::string language;

        std::vector<fs::path> includes;
        std::vector<fs::path> libraries;
        std::vector<fs::path> dynamicLibraries;
        std::vector<std::string> arguments;

        [[nodiscard]] std::optional<std::string> match(const std::string &header) const;

        explicit LibraryDocument(const std::string &json, const fs::path &root);
    };

    struct ManagerFile {
        Manager &manager;

        fs::path path;

        std::string type;

        std::unique_ptr<hermes::State> state;
        std::unique_ptr<parser::Root> root;

        std::set<std::tuple<fs::path, std::string>> dependencies;

        void resolve(std::unordered_set<const ManagerFile *> &visited) const;
        [[nodiscard]] std::unordered_set<const ManagerFile *> resolve() const;

        ManagerFile(Manager &manager, fs::path path, std::string type, const LibraryDocument *library = nullptr);
    };

    struct ManagerTarget {
        std::string triple;
        const llvm::Target *target;
        llvm::TargetMachine *machine;
        std::unique_ptr<llvm::DataLayout> layout;

        [[nodiscard]] bool valid() const;

        explicit ManagerTarget(const std::string &suggestedTriple);
    };

    struct Manager {
        const options::Options &options;

        ManagerTarget target;

        std::vector<LibraryDocument> libraries;

        // Param 1 is absolute path
        std::unordered_map<std::string, std::unique_ptr<ManagerFile>> nodes;

        std::unique_ptr<llvm::LLVMContext> context;

        Builder create(const ManagerFile &file);
        const ManagerFile &get(const fs::path &path, const fs::path &root = "", const std::string &type = "");

        explicit Manager(const options::Options &options);
    };
}
