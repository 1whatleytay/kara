#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>
#include <builder/operations.h>

#include <parser/assign.h>
#include <parser/function.h>
#include <parser/operator.h>
#include <parser/search.h>
#include <parser/statement.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <map>

namespace kara::builder {
    void Function::build() {
        bool responsible = parser::search::exclusive::root(node) == builder.root;

        utils::Typename returnTypename = from(utils::PrimitiveType::Nothing);

        const hermes::Node *body;

        switch (node->is<parser::Kind>()) {
        case parser::Kind::Function: {
            auto e = node->as<parser::Function>();

            if (auto fixed = e->fixedType())
                returnTypename = builder.resolveTypename(fixed);

            body = responsible ? e->body() : nullptr;
            // Check for inferred type from expression node maybe?
            if (body && body->is(parser::Kind::Expression) && !e->hasFixedType
                && returnTypename == from(utils::PrimitiveType::Nothing)) {

                builder::Scope productScope(body, *this, false);

                returnTypename = productScope.product.value().type;
            }

            returnType = builder.makeTypename(returnTypename);

            std::vector<utils::Typename> parameters(e->parameterCount);
            std::vector<llvm::Type *> parameterTypes(e->parameterCount);

            auto parameterVariables = e->parameters();

            for (size_t a = 0; a < e->parameterCount; a++) {
                auto fixed = parameterVariables[a]->fixedType();

                if (!fixed) {
                    throw VerifyError(e->children[a].get(),
                        "Function parameter must have given type, default "
                        "parameters are not implemented.");
                }

                parameters[a] = builder.resolveTypename(fixed);
                parameterTypes[a] = builder.makeTypename(parameters[a]);
            }

            type = { utils::FunctionTypename::Kind::Pointer, std::move(parameters),
                std::make_shared<utils::Typename>(returnTypename) };

            llvm::FunctionType *valueType = llvm::FunctionType::get(returnType, parameterTypes, false);
            function = llvm::Function::Create(
                valueType, llvm::GlobalVariable::ExternalLinkage, 0, e->name, builder.module.get());

            break;
        }

        case parser::Kind::Type: {
            assert(purpose == Purpose::TypeDestructor); // no weirdness please

            auto e = node->as<parser::Type>();
            auto structType = builder.makeType(e);

            auto voidType = llvm::Type::getVoidTy(builder.context);
            auto paramType = llvm::PointerType::get(structType->type, 0);

            auto name = fmt::format("{}_implicit_dest", e->name);

            llvm::FunctionType *valueType = llvm::FunctionType::get(voidType, { paramType }, false);
            function = llvm::Function::Create(
                valueType, llvm::GlobalVariable::ExternalLinkage, 0, name, builder.module.get());

            body = responsible ? e : nullptr;

            break;
        }

        default:
            throw;
        }

        if (body) {
            entryBlock = llvm::BasicBlock::Create(builder.context, "entry", function);
            exitBlock = llvm::BasicBlock::Create(builder.context, "exit", function);

            entry.SetInsertPoint(entryBlock);
            exit.SetInsertPoint(exitBlock);

            if (returnTypename != from(utils::PrimitiveType::Nothing))
                returnValue = entry.CreateAlloca(returnType, nullptr, "result");

            builder::Scope scope(body, *this);

            if (body->is(parser::Kind::Expression)) {
                if (!scope.product.has_value())
                    throw VerifyError(body, "Missing product for expression type function.");

                builder::Result result = scope.product.value();

                auto context = ops::Context::from(scope);

                std::optional<builder::Result> resultConverted = ops::makeConvert(context, result, *type.returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(body, "Method returns type {} but expression is of type {}.",
                        toString(*type.returnType), toString(result.type));
                }

                result = resultConverted.value();

                scope.current.value().CreateStore(ops::get(context, result), returnValue);
            }

            entry.CreateBr(scope.openingBlock);

            if (body->is(parser::Kind::Code)) {
                scope.destinations[Scope::ExitPoint::Regular] = exitBlock;
                scope.destinations[Scope::ExitPoint::Return] = exitBlock;
                scope.commit();
            } else {
                scope.current->CreateBr(exitBlock);
            }

            if (returnTypename == from(utils::PrimitiveType::Nothing))
                exit.CreateRetVoid();
            else
                exit.CreateRet(exit.CreateLoad(returnValue, "final"));
        }
    }

    Function::Function(const hermes::Node *node, Builder &builder)
        : builder(builder)
        , node(node)
        , entry(builder.context)
        , exit(builder.context) {
        purpose = ([node]() {
            switch (node->is<parser::Kind>()) {
            case parser::Kind::Function:
                return Purpose::UserFunction;
            case parser::Kind::Type:
                return Purpose::TypeDestructor;
            default:
                throw;
            }
        })();
    }
}
