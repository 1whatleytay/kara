#include <builder/manager.h>

#include <builder/builder.h>
#include <builder/error.h>

#include <parser/import.h>
#include <parser/literals.h>

#include <interfaces/interfaces.h>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>

// Passes
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

// Analysis
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LazyValueInfo.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace kara::builder {
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

    void ManagerFile::resolve(std::unordered_set<const ManagerFile *> &visited) const { // NOLINT(misc-no-recursion)
        visited.insert(this);

        for (const auto &[depPath, depType] : dependencies) {
            const ManagerFile *f = &manager.get(depPath, fs::path(path).parent_path().string(), depType);

            if (visited.find(f) != visited.end())
                continue;

            f->resolve(visited);
        }
    }

    std::unordered_set<const ManagerFile *> ManagerFile::resolve() const {
        std::unordered_set<const ManagerFile *> result;
        resolve(result);

        return result;
    }

    ManagerFile::ManagerFile(Manager &manager, fs::path path, std::string type, const LibraryDocument *library)
        : manager(manager)
        , path(std::move(path))
        , type(std::move(type)) {
        if (this->type.empty() || this->type == "kara") {
            std::stringstream buffer;

            {
                std::ifstream stream(this->path);
                buffer << stream.rdbuf();
            }

            try {
                state = std::make_unique<hermes::State>(buffer.str());

                root = std::make_unique<parser::Root>(*state);
            } catch (const hermes::ParseError &error) {
                hermes::LineDetails details(state->text, error.index);

                fmt::print("{} [line {}]\n{}\n{}\n", error.issue, details.lineNumber, details.line, details.marker);

                throw;
            }
        } else if (this->type == "c") {
            assert(library && library->language == this->type);

            auto thisPath = fs::current_path();
            auto fullPath = fs::absolute(this->path);

            std::vector<const char *> arguments = { thisPath.c_str(), fullPath.c_str() };
            arguments.reserve(arguments.size() + library->arguments.size());

            auto cString = [](const auto &s) { return s.c_str(); };
            std::transform(
                library->arguments.begin(), library->arguments.end(), std::back_inserter(arguments), cString);

            auto [tupleState, tupleRoot]
                = interfaces::header::create(static_cast<int>(arguments.size()), arguments.data());

            state = std::move(tupleState);
            root = std::move(tupleRoot);
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

    bool ManagerTarget::valid() const { return !triple.empty() && target && machine && layout; }

    ManagerTarget::ManagerTarget(const std::string &suggestedTriple) {
        triple = suggestedTriple.empty() ? llvm::sys::getDefaultTargetTriple() : suggestedTriple;

        LLVMInitializeX86TargetInfo();

        LLVMInitializeX86Target();
        LLVMInitializeX86TargetMC();

        LLVMInitializeX86AsmParser();
        LLVMInitializeX86AsmPrinter();

        std::string error;
        target = llvm::TargetRegistry::lookupTarget(triple, error);

        llvm::TargetOptions targetOptions;
        llvm::Optional<llvm::Reloc::Model> model;
        machine = target->createTargetMachine(triple, "generic", "", targetOptions, model);

        layout = std::make_unique<llvm::DataLayout>(machine->createDataLayout());
    }


    const LibraryDocument &Manager::add(const fs::path &path) {
        auto absolute = fs::absolute(path);
        auto it = libraries.find(absolute.string());
        if (it != libraries.end())
            return *it->second;

        std::ifstream stream(absolute);
        if (!stream.good())
            throw std::runtime_error(fmt::format("Could not load from stream {}.", path.string()));

        std::stringstream buffer;
        buffer << stream.rdbuf();

        auto library = std::make_unique<LibraryDocument>(buffer.str(), path.parent_path());
        auto &ref = *library;
        auto out = libraries.insert({ absolute.string(), std::move(library) });

        return ref;
    }

    const ManagerFile &Manager::get(const fs::path &path, const fs::path &root, const std::string &type) {
        fs::path fullPath = path.is_absolute() ? path : root / path;
        std::string absoluteName = fs::absolute(fullPath).string();

        const LibraryDocument *doc = nullptr;

        if (!fs::exists(fullPath)) {
            bool matches = false;

            if (path.is_relative()) {
                for (const auto &library : libraries) {
                    std::optional<std::string> subPath = library.second->match(path);

                    if (subPath) {
                        fullPath = *subPath;
                        doc = library.second.get();
                        matches = true;

                        break;
                    }
                }
            }

            if (!matches)
                throw std::runtime_error(fmt::format("Cannot find file under path {}.", path.string()));
        }

        auto iterator = nodes.find(fullPath);
        if (iterator != nodes.end())
            return *iterator->second;

        if (callback)
            callback(path, type);

        auto file = std::make_unique<ManagerFile>(*this, fullPath, type, doc);
        auto *ref = file.get();
        nodes[fullPath] = std::move(file);

        return *ref;
    }

    Manager::Manager(const std::string &triple, ManagerCallback callback)
        : context(std::make_unique<llvm::LLVMContext>())
        , target(triple)
        , callback(std::move(callback)) {

        if (!target.valid())
            throw std::runtime_error("Could not initialize target.");

//        std::unique_ptr<llvm::Module> base;
//        std::optional<llvm::Linker> linker;
//
//        auto buildFile = [&](const ManagerFile &file) {
//            Builder b = build(file);
//
//            if (!linker) {
//                base = std::move(b.module);
//                linker.emplace(*base);
//            } else {
//                linker->linkInModule(std::move(b.module));
//            }
//        };
//
//        for (const auto &s : options.inputs) {
//            auto &file = get(s);
//
//            built.insert(&file);
//            buildFile(file);
//        }

        // build all depended on files too, this can be handled by build system
//        if (options.interpret) {
//            for (const auto &pair : nodes) {
//                if (built.find(pair.second.get()) == built.end()) {
//                    if (pair.second->type != "c") {
//                        buildFile(*pair.second);
//                    }
//                }
//            }
//        }

//        linker.reset();
//
//        callback(ManagerCallbackReason::Cleanup, "", "");
//
//        if (verifyModule(*base, &llvm::errs(), nullptr)) {
//            base->print(llvm::outs(), nullptr);
//
//            throw std::runtime_error("Aborted.");
//        }
    }
}
