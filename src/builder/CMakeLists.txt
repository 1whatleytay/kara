add_library(builder STATIC
    include/builder/manager.h
    include/builder/builder.h
    include/builder/error.h
    include/builder/operations.h
    include/builder/handlers.h
	include/builder/builtins.h
	include/builder/library.h
	include/builder/platform.h
	include/builder/target.h

    src/manager.cpp
	src/target.cpp
    src/builder.cpp
    src/error.cpp
	src/platform.cpp
    src/function.cpp
    src/typename.cpp
    src/variable.cpp
    src/reference.cpp
    src/result.cpp
    src/destroy.cpp
    src/type.cpp
	src/library.cpp
    src/operations/call.cpp
	src/operations/utilities.cpp
	src/operations/operations.cpp
	src/operations/expression.cpp
	src/operations/handlers/expression.cpp
	src/operations/handlers/operations.cpp
	src/operations/handlers/builtins.cpp
	src/operations/statements.cpp)

target_include_directories(builder PUBLIC include)
target_link_libraries(builder PUBLIC parser options llvm interfaces)
target_precompile_headers(builder PUBLIC include/builder/pch.h)
