add_library(parser STATIC
	include/parser/kinds.h
    include/parser/root.h
    include/parser/function.h
    include/parser/variable.h
    include/parser/typename.h
    include/parser/assign.h
    include/parser/expression.h
    include/parser/operator.h
    include/parser/statement.h
    include/parser/scope.h
    include/parser/reference.h
    include/parser/number.h
    include/parser/parentheses.h
    include/parser/block.h
    include/parser/unary.h
    include/parser/call.h
    include/parser/debug.h
    include/parser/if.h
    include/parser/for.h
    include/parser/bool.h
	
	src/root.cpp
    src/function.cpp
    src/variable.cpp
    src/typename.cpp
    src/assign.cpp
    src/expression.cpp
    src/operator.cpp
    src/statement.cpp
    src/code.cpp
    src/reference.cpp
    src/number.cpp
    src/parentheses.cpp
    src/block.cpp
    src/unary.cpp
    src/call.cpp
    src/debug.cpp
    src/if.cpp
    src/for.cpp
    src/bool.cpp)

target_include_directories(parser PUBLIC include)
target_link_libraries(parser PUBLIC fmt hermes)
