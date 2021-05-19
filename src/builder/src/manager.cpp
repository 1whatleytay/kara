#include <builder/manager.h>

#include <builder/error.h>
#include <builder/builder.h>

#include <parser/import.h>
#include <parser/literals.h>

#include <interfaces/interfaces.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/Host.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include <rapidjson/document.h>

#include <fstream>
#include <sstream>

std::optional<std::string> LibraryDocument::match(const std::string &header) const {
    for (const auto &include : includes) {
        fs::path test = include / header;

        if (fs::exists(test)) {
            return test;
        }
    }

    return std::nullopt;
}

LibraryDocument::LibraryDocument(const std::string &json, const fs::path &root) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    language = doc["language"].GetString();

    assert(language == "c");

    for (const auto &i : doc["includes"].GetArray()) {
        fs::path k = i.GetString();

        if (k.is_absolute())
            includes.push_back(k);
        else
            includes.push_back(root / k);
    }

    for (const auto &i : doc["libraries"].GetArray()) {
        fs::path k = i.GetString();

        if (k.is_absolute())
            libraries.push_back(k);
        else
            libraries.push_back(root / k);
    }

    for (const auto &i : doc["dynamic-libraries"].GetArray()) {
        fs::path k = i.GetString();

        if (k.is_absolute())
            dynamicLibraries.push_back(k);
        else
            dynamicLibraries.push_back(root / k);
    }

    for (const auto &k : doc["arguments"].GetArray())
        arguments.emplace_back(k.GetString());
}

void ManagerFile::resolve(std::set<const ManagerFile *> &visited) const {
    visited.insert(this);

    for (const auto &d : dependencies) {
        const ManagerFile *f = &manager.get(std::get<0>(d), fs::path(path).parent_path().string(), std::get<1>(d));

        if (visited.find(f) != visited.end())
            continue;

        f->resolve(visited);
    }
}

std::set<const ManagerFile *> ManagerFile::resolve() const {
    std::set<const ManagerFile *> result;
    resolve(result);

    return result;
}

ManagerFile::ManagerFile(Manager &manager, fs::path path, std::string type, const LibraryDocument *library)
    : manager(manager), path(std::move(path)), type(std::move(type)) {
    if (this->type.empty() || this->type == "kara") {
        std::stringstream buffer;

        {
            std::ifstream stream(this->path);
            buffer << stream.rdbuf();
        }

        try {
            state = std::make_unique<State>(buffer.str());

            root = std::make_unique<RootNode>(*state);
        } catch (const ParseError &error) {
            LineDetails details(state->text, error.index);

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
        std::transform(library->arguments.begin(), library->arguments.end(), std::back_inserter(arguments), cString);

        auto [tupleState, tupleRoot] = interfaces::header::create(static_cast<int>(arguments.size()), arguments.data());

        state = std::move(tupleState);
        root = std::move(tupleRoot);
    } else {
        throw std::exception();
    }

    for (const auto &e : root->children) {
        if (!e->is(Kind::Import))
            continue;

        auto import = e->as<ImportNode>();
        auto *string = import->body();

        if (!string->inserts.empty())
            throw VerifyError(string, "Imported string node cannot have any inserts.");

        dependencies.insert({ string->text, import->type });
    }
}


bool ManagerTarget::valid() const {
    return !triple.empty() && target && machine && layout;
}

ManagerTarget::ManagerTarget(const std::string &suggestedTriple) {
    triple = suggestedTriple.empty() ? sys::getDefaultTargetTriple() : suggestedTriple;

    LLVMInitializeX86TargetInfo();

    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();

    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    std::string error;
    target = TargetRegistry::lookupTarget(triple, error);

    TargetOptions targetOptions;
    Optional<Reloc::Model> model;
    machine = target->createTargetMachine(triple, "generic", "", targetOptions, model);

    layout = std::make_unique<DataLayout>(machine->createDataLayout());
}

Builder Manager::create(const ManagerFile &file) {
#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
    try {
        return Builder(file, options);
    } catch (const VerifyError &error) {
        LineDetails details(file.state->text, error.node->index, false);

        fmt::print("{} [line {}]\n{}\n{}\n", error.issue, details.lineNumber, details.line, details.marker);

        throw;
    }
#pragma clang diagnostic pop
}

const ManagerFile &Manager::get(const fs::path &path, const fs::path &root, const std::string &type) {
    fs::path fullPath = path.is_absolute() ? path : root / path;
    std::string absoluteName = fs::absolute(fullPath).string();

    const LibraryDocument *doc = nullptr;

    if (!fs::exists(fullPath)) {
        bool matches = false;

        if (path.is_relative()) {
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
            throw std::runtime_error(fmt::format("Cannot find file under path {}.", path.string()));
    }

    auto iterator = nodes.find(fullPath);
    if (iterator != nodes.end())
        return *iterator->second;

    auto file = std::make_unique<ManagerFile>(*this, fullPath, type, doc);
    auto &ref = *file;
    nodes[fullPath] = std::move(file);

    return ref;
}

Manager::Manager(const Options &options)
    : context(std::make_unique<LLVMContext>()), target(options.triple), options(options) {

    if (!target.valid())
        throw std::runtime_error("Could not initialize target.");

    for (const std::string &library : options.libraries) {
        std::ifstream stream(library);
        if (!stream.good())
            throw std::runtime_error(fmt::format("Could not load from stream {}.", library));

        std::stringstream buffer;
        buffer << stream.rdbuf();

        libraries.emplace_back(buffer.str(), library);
    }

    std::unique_ptr<Module> base;
    std::optional<Linker> linker;

    for (const auto &s : options.inputs) {
        Builder b = create(get(s));

        if (!linker) {
            base = std::move(b.module);
            linker.emplace(*base);
        } else {
            linker->linkInModule(std::move(b.module));
        }
    }

    if (options.printIR)
        base->print(llvm::outs(), nullptr);

    if (verifyModule(*base, &llvm::errs(), nullptr))
        throw std::runtime_error("Aborted.");

    if (!options.output.empty()) {
        legacy::PassManager pass;

        std::error_code error;
        raw_fd_ostream output(options.output, error);

        if (error)
            throw std::runtime_error(fmt::format("Cannot open file {} for output", options.output));

        if (target.machine->addPassesToEmitFile(pass, output, nullptr, CodeGenFileType::CGFT_ObjectFile))
            throw std::runtime_error("Target machine does not support object output.");

        pass.run(*base);
    }

    if (options.interpret) {
        auto expectedJit = orc::LLJITBuilder().create();

        if (!expectedJit)
            throw std::runtime_error("Could not create jit instance.");

        auto &jit = expectedJit.get();

        if (jit->addIRModule(orc::ThreadSafeModule(std::move(base), std::move(context))))
            throw std::runtime_error("Could not add module to jit instance.");

        for (const auto &library : libraries) {
            for (const auto &lib : library.libraries) {
                auto bufferOrError = MemoryBuffer::getFileAsStream(lib.string());

                if (!bufferOrError) {
                    fmt::print("Failed to load library {}.\n", lib.string());
                } else {
                    auto loader = llvm::orc::StaticLibraryDefinitionGenerator::Load(
                        jit->getObjLinkingLayer(), lib.c_str());

                    if (!loader) {
                        fmt::print("Error: {}\n", toString(loader.takeError()));
                        fmt::print("Failed to load library {}.\n", lib.string());
                    } else {
                        jit->getMainJITDylib().addGenerator(std::move(loader.get()));
                    }
                }
            }

            for (const auto &lib : library.dynamicLibraries) {
                auto loader = llvm::orc::DynamicLibrarySearchGenerator::Load(
                    lib.c_str(), target.layout->getGlobalPrefix());

                if (!loader) {
                    fmt::print("Failed to load dynamic library {}.\n", lib.string());
                } else {
                    jit->getMainJITDylib().addGenerator(std::move(loader.get()));
                }
            }
        }

        auto expectedMain = jit->lookup("main");
        if (!expectedMain)
            throw std::runtime_error("Could not find main symbol.");

        auto *entry = reinterpret_cast<int(*)()>(expectedMain.get().getAddress());

        fmt::print("Returned {}.\n", entry());
    }
}
