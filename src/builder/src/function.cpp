#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>
#include <builder/operations.h>

#include <parser/assign.h>
#include <parser/function.h>
#include <parser/operator.h>
#include <parser/scope.h>
#include <parser/search.h>
#include <parser/statement.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <map>
#include <cassert>

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

                auto astFunction = node->as<parser::Function>();
                auto parameters = astFunction->parameters();

                // this might be bad?
                Cache tempCache;

                ops::Context tempContext = { builder };
                tempContext.function = this;
                tempContext.cache = &tempCache;

                for (auto parameter : parameters) {
                    tempCache.variables.insert({
                        parameter,
                        std::make_unique<builder::Variable>(parameter, tempContext, nullptr),
                    });
                }

                auto expressionValue = ops::expression::make(tempContext, body->as<parser::Expression>());
                returnTypename = expressionValue.type;
            }

            returnType = builder.makeTypename(returnTypename);

            // sad
            utils::FunctionParameters parameters(e->parameterCount);
            std::vector<std::pair<std::string, llvm::Type *>> parameterTypes(e->parameterCount);

            auto parameterVariables = e->parameters();

            for (size_t a = 0; a < e->parameterCount; a++) {
                auto var = parameterVariables[a];

                if (!var->hasFixedType) {
                    throw VerifyError(e->children[a].get(),
                        "Function parameter must have given type, default "
                        "parameters are not implemented.");
                }

                auto varTypename = builder.resolveTypename(var->fixedType());
                auto varLLVMType = builder.makeTypename(varTypename);

                parameters[a] = { var->name, varTypename };
                parameterTypes[a] = { var->name, varLLVMType };
            }

            type = {
                utils::FunctionKind::Pointer,
                std::move(parameters),
                std::make_shared<utils::Typename>(returnTypename),
            };

            rawArguments = { returnType, parameterTypes };
            auto formattedArguments = builder.platform->formatArguments(builder.target, rawArguments);

            llvm::FunctionType *valueType = llvm::FunctionType::get(
                formattedArguments.returnType, formattedArguments.parameterTypes(), e->isCVarArgs);
            function = llvm::Function::Create(
                valueType, llvm::GlobalVariable::ExternalLinkage, 0, e->name, builder.module.get());

            // name function parameters
            for (size_t a = 0; a < formattedArguments.parameters.size(); a++) {
                auto arg = function->getArg(a);
                auto &[name, _, attrs] = formattedArguments.parameters[a];

                arg->setName(name);
                arg->addAttrs(attrs);
            }

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

            std::vector<llvm::Argument *> arguments(function->arg_size());

            for (size_t a = 0; a < arguments.size(); a++)
                arguments[a] = function->getArg(a);

            if (node->is(parser::Kind::Function)) {
                ops::Context entryContext {
                    builder,
                    nullptr,

                    &entry,

                    &cache,
                    this,

                    nullptr,
                };

                auto astFunction = node->as<parser::Function>();
                auto parameters = astFunction->parameters();

                auto realArguments = builder.platform->tieArguments(
                    entryContext, rawArguments.returnType, rawArguments.parameterTypes(), arguments);

                // Create parameters within scope.
                for (size_t a = 0; a < parameters.size(); a++) {
                    auto parameterNode = parameters[a];

                    cache.variables[parameterNode]
                        = std::make_unique<builder::Variable>(parameterNode, entryContext, realArguments[a]);
                }
            }

            if (returnTypename != from(utils::PrimitiveType::Nothing))
                returnValue = entry.CreateAlloca(returnType, nullptr, "result");

            switch (body->is<parser::Kind>()) {
            case parser::Kind::Expression: {
                auto bodyBlock = llvm::BasicBlock::Create(builder.context, "", function, exitBlock);
                llvm::IRBuilder<> bodyBuilder(bodyBlock);

                ops::Context bodyContext {
                    builder,
                    nullptr, // accumulator is null?

                    &bodyBuilder,

                    &cache,
                    this,

                    // exit info might need to be added later for ?? but right now we give resp. to makeScope
                    nullptr,
                };

                auto result = ops::expression::make(bodyContext, body->as<parser::Expression>());

                auto resultConverted = ops::makeConvert(bodyContext, result, *type.returnType);

                if (!resultConverted) {
                    throw VerifyError(body, "Method returns type {} but expression is of type {}.",
                        toString(*type.returnType), toString(result.type));
                }

                bodyBuilder.CreateStore(ops::get(bodyContext, *resultConverted), returnValue);

                entry.CreateBr(bodyBlock);
                bodyBuilder.CreateBr(exitBlock);

                break;
            }

            case parser::Kind::Code: {
                ops::Context bodyContext {
                    builder,
                    nullptr, // accumulator is null?

                    &entry,

                    &cache, this,

                    nullptr, // exit info might need to be added later for ?? but right now we give resp. to makeScope
                };

                auto scope = ops::statements::makeScope(bodyContext, body->as<parser::Code>(),
                    {
                        { ExitPoint::Regular, exitBlock },
                        { ExitPoint::Return, exitBlock },
                    });

                entry.CreateBr(scope);

                break;
            }

            case parser::Kind::Type: { // create destructor for elements
                assert(purpose == builder::Function::Purpose::TypeDestructor); // no mistakes

                auto e = node->as<parser::Type>();

                if (e->isAlias)
                    return;

                auto targetType = builder.makeType(e);
                auto fields = e->fields();

                assert(function->arg_size() > 0);

                llvm::Argument *arg = function->getArg(0);

                { // sanity checks
                    auto underlyingType = arg->getType();
                    assert(underlyingType->isPointerTy());

                    auto elementType = underlyingType->getPointerElementType();
                    assert(elementType == targetType->type);
                }

                auto bodyBlock = llvm::BasicBlock::Create(builder.context, "", function, entryBlock->getNextNode());

                llvm::IRBuilder<> bodyBuilder(bodyBlock);

                ops::Context bodyContext {
                    builder,
                    nullptr,

                    &bodyBuilder,

                    nullptr,
                    this,

                    nullptr,
                };

                for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
                    auto var = *it;
                    auto index = targetType->indices.at(var);

                    assert(var->hasFixedType);

                    // // TODO might need mutable/immutable versions of implicit destructors
                    // auto result = builder::Result {
                    //     builder::Result::FlagReference,
                    //     current->CreateStructGEP(arg, index),
                    //     builder.resolveTypename(var->fixedType()),
                    //     &accumulator, // might as well
                    // };

                    ops::makeDestroy(bodyContext, bodyBuilder.CreateStructGEP(targetType->type, arg, index),
                        builder.resolveTypename(var->fixedType()));
                }

                entry.CreateBr(bodyBlock);
                bodyBuilder.CreateBr(exitBlock);

                break;
            }

            default:
                throw;
            }

            ops::Context exitContext {
                builder,
                nullptr,

                &exit,

                nullptr,
                this,
            };

            if (returnTypename == from(utils::PrimitiveType::Nothing)) {
                builder.platform->tieReturn(exitContext, returnType, nullptr, arguments);
            } else {
                builder.platform->tieReturn(
                    exitContext, returnType, exit.CreateLoad(returnType, returnValue, "final"), arguments);
            }
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
