#pragma once

#include <builder/library.h>

#include <parser/root.h>

#include <options/options.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>

#include <filesystem>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace kara::builder {
    struct Builder;
    struct Manager;

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

    using ManagerCallback = std::function<void(const fs::path &path, const std::string &type)>;

    struct Manager {
        ManagerTarget target;

        ManagerCallback callback;

        std::unique_ptr<llvm::LLVMContext> context;

        // key is absolute path
        std::unordered_map<std::string, std::unique_ptr<LibraryDocument>> libraries;
        std::unordered_map<std::string, std::unique_ptr<ManagerFile>> nodes;

        const LibraryDocument &add(const fs::path &library);

        const ManagerFile &get(const fs::path &path, const fs::path &root = "", const std::string &type = "");

        explicit Manager(const std::string &triple, ManagerCallback callback = ManagerCallback());
    };
}
