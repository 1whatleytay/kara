#include <builder/builder.h>

#include <builder/error.h>

#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

void BuilderFunction::build() {
    const Node *body = node->children[node->parameterCount].get();
    // Check for inferred type from expression node maybe?
    if (node->returnType == types::nothing() && body->is(Kind::Expression)) {
        returnTypename = BuilderScope(body, *this, false).product.value().type;
    } else {
        returnTypename = node->returnType;
    }

    returnType = builder.makeTypename(returnTypename);

    std::vector<Typename> parameters(node->parameterCount);
    std::vector<Type *> parameterTypes(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++) {
        const auto &fixed = node->children[a]->as<VariableNode>()->fixedType;

        if (!fixed) {
            throw VerifyError(node->children[a].get(),
                "Function parameter must have given type, default parameters are not implemented.");
        }

        parameters[a] = *fixed;
        parameterTypes[a] = builder.makeTypename(*fixed);
    }

    type = {
        FunctionTypename::Kind::Pointer,
        std::make_shared<Typename>(returnTypename),
        std::move(parameters)
    };

    FunctionType *valueType = FunctionType::get(returnType, parameterTypes, false);
    function = Function::Create(valueType, GlobalVariable::ExternalLinkage, 0, node->name, builder.module.get());

    entryBlock = BasicBlock::Create(*builder.context, "entry", function);
    exitBlock = BasicBlock::Create(*builder.context, "exit", function);

    entry.SetInsertPoint(entryBlock);
    exit.SetInsertPoint(exitBlock);

    if (returnTypename != types::nothing())
        returnValue = entry.CreateAlloca(returnType, nullptr, "result");

    const Node *bodyNode = node->children[node->parameterCount].get();

    BuilderScope scope(bodyNode, *this);

    if (bodyNode->is(Kind::Expression)) {
        if (!scope.product.has_value())
            throw VerifyError(bodyNode, "Missing product for expression type function.");

        BuilderResult result = scope.product.value();

        std::optional<BuilderResult> resultConverted = scope.convert(result, *type.returnType);

        if (!resultConverted.has_value()) {
            throw VerifyError(bodyNode,
                "Method returns type {} but expression is of type {}.",
                toString(*type.returnType), toString(result.type));
        }

        result = resultConverted.value();

        scope.current.value().CreateStore(scope.get(result), returnValue);
    }

    entry.CreateBr(scope.openingBlock);

    if (!scope.currentBlock->getTerminator())
        scope.current.value().CreateBr(exitBlock);

    if (returnTypename == types::nothing())
        exit.CreateRetVoid();
    else
        exit.CreateRet(exit.CreateLoad(returnValue, "final"));
}

BuilderFunction::BuilderFunction(const FunctionNode *node, Builder &builder)
    : builder(builder), node(node), entry(*builder.context), exit(*builder.context) { }
