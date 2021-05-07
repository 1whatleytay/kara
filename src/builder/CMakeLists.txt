add_library(builder STATIC
	include/builder/manager.h
	include/builder/builder.h
    include/builder/error.h

	src/manager.cpp
	src/builder.cpp
    src/error.cpp
    src/function.cpp
    src/typename.cpp
    src/variable.cpp
    src/assign.cpp
    src/reference.cpp
    src/expression.cpp
    src/statement.cpp
    src/scope.cpp
	src/debug.cpp
	src/type.cpp
    src/if.cpp
    src/for.cpp
    src/block.cpp)

target_include_directories(builder PUBLIC include)
target_link_libraries(builder PUBLIC parser options interfaces llvm rapidjson)
