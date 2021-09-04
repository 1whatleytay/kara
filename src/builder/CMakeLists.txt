add_library(builder STATIC
    include/builder/manager.h
    include/builder/builder.h
    include/builder/error.h
    include/builder/operations.h
    include/builder/handlers.h

    src/manager.cpp
    src/builder.cpp
    src/error.cpp
    src/function.cpp
    src/typename.cpp
    src/variable.cpp
    src/assign.cpp
    src/reference.cpp
    src/result.cpp
    src/destroy.cpp
    src/statement.cpp
    src/scope.cpp
    src/type.cpp
    src/block.cpp
	src/operations/call.cpp
	src/operations/utilities.cpp
	src/operations/operations.cpp
	src/operations/expression.cpp
	src/operations/handlers/expression.cpp
	src/operations/handlers/operations.cpp)

target_include_directories(builder PUBLIC include)
target_link_libraries(builder PUBLIC parser options llvm rapidjson interfaces)
