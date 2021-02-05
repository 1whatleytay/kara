add_library(builder STATIC
	include/builder/builder.h
    include/builder/error.h
	include/builder/search.h

    include/builder/lifetime/lifetime.h
	include/builder/lifetime/multiple.h
	include/builder/lifetime/null.h
	include/builder/lifetime/variable.h
	include/builder/lifetime/reference.h
	include/builder/lifetime/array.h

	src/builder.cpp
    src/error.cpp
    src/function.cpp
    src/typename.cpp
    src/variable.cpp
    src/assign.cpp
	src/search.cpp
    src/reference.cpp
    src/expression.cpp
    src/statement.cpp
    src/scope.cpp
	src/debug.cpp
	src/scope-lifetime.cpp
    src/if.cpp
    src/for.cpp
    src/block.cpp

	src/lifetime/lifetime.cpp
	src/lifetime/multiple.cpp
	src/lifetime/null.cpp
	src/lifetime/variable.cpp
	src/lifetime/reference.cpp
	src/lifetime/array.cpp)

target_include_directories(builder PUBLIC include)
target_link_libraries(builder PUBLIC parser options llvm)
