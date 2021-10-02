#pragma once

#include <options/options.h>

#include <utils/expression.h>
#include <utils/typename.h>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace kara::parser {
    struct As;
    struct If;
    struct For;
    struct Dot;
    struct New;
    struct Call;
    struct Code;
    struct Root;
    struct Type;
    struct Block;
    struct Index;
    struct Assign;
    struct Import;
    struct Ternary;
    struct Function;
    struct Variable;
    struct Reference;
    struct Statement;
    struct Expression;

    enum class Kind;
}

namespace kara::builder {
    struct Manager;
    struct ManagerFile;

    struct Builder;

    //    struct Scope;
    struct Result;
    struct Function;
    struct Accumulator;

    // Getting rid of this would give bonus points, used for Unresolved
    namespace ops {
        struct Context;
    }

    namespace ops::matching {
        struct MatchInput;
    }

    namespace ops::handlers::builtins {
        using Parameters = ops::matching::MatchInput;
        using BuiltinFunction = std::optional<builder::Result> (*)(const Context &, const Parameters &);
    }

    struct Result {
        // valid flag layouts: reference, temporary | mutable | variable
        enum Flags : uint32_t {
            FlagTemporary = 1u << 0u,
            FlagMutable = 1u << 1u,
            FlagReference = 1u << 2u,
        };

        uint32_t flags = 0;
        [[nodiscard]] bool isSet(Flags flag) const;

        uint64_t uid = 0; // copied when needed, for reference in move without refactor

        llvm::Value *value = nullptr;
        utils::Typename type;

        Result(uint32_t flags, llvm::Value *value, utils::Typename type, Accumulator *accumulator);
    };

    struct Unresolved {
        const hermes::Node *from = nullptr;
        std::vector<const hermes::Node *> references;
        std::vector<ops::handlers::builtins::BuiltinFunction> builtins;

        std::shared_ptr<builder::Result> implicit; // std::optional?

        Unresolved(const hermes::Node *from, std::vector<const hermes::Node *> references,
            std::vector<ops::handlers::builtins::BuiltinFunction> builtins, std::unique_ptr<Result> implicit = nullptr);
    };

    using Wrapped = std::variant<Result, Unresolved>;

    // Thinking struct for destroying objects when a statement is done.
    struct Accumulator {
        uint64_t nextUID = 1;
        uint64_t getNextUID();

        bool lock = false; // for debugging consider/commit loops

        std::queue<Result> toDestroy;
        std::unordered_set<uint64_t> avoidDestroy;

        void consider(const Result &result);

        void commit(const ops::Context &context);
    };

    struct Variable {
        const parser::Variable *node = nullptr;
        utils::Typename type;

        llvm::Value *value = nullptr;

        // global variable
        Variable(const parser::Variable *node, builder::Builder &builder);
        // regular variable
        Variable(const parser::Variable *node, const ops::Context &context);
        // function parameter
        Variable(const parser::Variable *node, const ops::Context &context, llvm::Argument *argument);
    };

    struct Cache {
        builder::Cache *parent = nullptr;

        std::vector<std::unique_ptr<builder::Cache>> children; // probably don't need this in favor of in place caches

        builder::Cache *create();

        template <typename K, typename T>
        using Record = std::unordered_map<K, std::unique_ptr<T>>;

        // separate for now... for data efficiency - use findVariable function
        Record<const parser::Variable *, builder::Variable> variables;
        // For types, might need to be cleared occasionally. For
        // ArrayKind::UnboundedSize mostly.
        Record<const parser::Expression *, builder::Result> expressions;

        template <typename K, typename T>
        T *find(Record<K, T> builder::Cache::*ref, K key) const {
            const Cache *current = this;

            while (current) {
                auto &map = current->*ref;

                auto it = map.find(key);
                if (it != map.end())
                    return it->second.get();

                current = current->parent;
            }

            return nullptr;
        }
    };

    enum class ExitPoint { Regular, Return, Break, Continue };

    struct Type {
        builder::Builder &builder;
        const parser::Type *node = nullptr;

        llvm::StructType *type = nullptr;

        std::unordered_map<const parser::Variable *, size_t> indices;

        Function *implicitDestructor = nullptr;

        // for avoiding recursive problems
        void build();

        explicit Type(const parser::Type *node, builder::Builder &builder);
    };

    struct Function {
        enum class Purpose {
            UserFunction,
            TypeDestructor,
        };

        Purpose purpose = Purpose::UserFunction;

        builder::Builder &builder;
        builder::Cache cache;

        const hermes::Node *node = nullptr;

        llvm::BasicBlock *entryBlock = nullptr;
        llvm::BasicBlock *exitBlock = nullptr;

        llvm::IRBuilder<> entry;
        llvm::IRBuilder<> exit;

        llvm::Type *returnType = nullptr;
        llvm::Value *returnValue = nullptr;

        utils::FunctionTypename type;
        llvm::Function *function = nullptr;

        void build();

        Function(const hermes::Node *node, Builder &builder);
    };

    struct Builder {
        const parser::Root *root = nullptr;

        const ManagerFile &file;
        const options::Options &options;

        llvm::Function *mallocCache = nullptr;
        llvm::Function *freeCache = nullptr;
        llvm::Function *reallocCache = nullptr;

        std::unordered_set<const ManagerFile *> dependencies;

        llvm::LLVMContext &context;
        std::unique_ptr<llvm::Module> module;

        std::vector<const hermes::Node *> destroyInvocables;

        std::unordered_map<const parser::Type *, std::unique_ptr<Function>> implicitDestructors;

        std::unordered_map<const parser::Type *, std::unique_ptr<builder::Type>> types;
        std::unordered_map<const parser::Variable *, std::unique_ptr<builder::Variable>> globals;
        std::unordered_map<const parser::Function *, std::unique_ptr<builder::Function>> functions;

        builder::Type *makeType(const parser::Type *node);
        builder::Variable *makeGlobal(const parser::Variable *node);
        builder::Function *makeFunction(const parser::Function *node);

        llvm::Function *getMalloc();
        llvm::Function *getFree();
        llvm::Function *getRealloc();

        //        const hermes::Node *find(const parser::Reference *node);
        std::vector<const hermes::Node *> findAll(const parser::Reference *node);

        using SearchChecker = std::function<bool(const hermes::Node *)>;

        const hermes::Node *searchDependencies(const SearchChecker &match);
        std::vector<const hermes::Node *> searchAllDependencies(const SearchChecker &match);

        utils::Typename resolveTypename(const hermes::Node *node);

        llvm::Type *makeTypename(const utils::Typename &type);
        [[nodiscard]] llvm::Type *makePrimitiveType(utils::PrimitiveType type) const;

        llvm::StructType *makeOptionalType(const utils::Typename &of);
        // take llvm::Type * ? can use hashmap
        llvm::StructType *makeVariableArrayType(const utils::Typename &of);

        Builder(const ManagerFile &file, const options::Options &opts);
    };
}
