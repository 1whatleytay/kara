add_library(parser STATIC
#    include/parser/kinds.h
    include/parser/search.h
    include/parser/root.h
    include/parser/function.h
    include/parser/variable.h
    include/parser/typename.h
    include/parser/assign.h
    include/parser/expression.h
    include/parser/operator.h
    include/parser/statement.h
    include/parser/scope.h
    include/parser/literals.h
    include/parser/type.h
    include/parser/import.h

    src/search.cpp
    src/root.cpp
    src/function.cpp
    src/variable.cpp
    src/typename.cpp
    src/assign.cpp
    src/literals.cpp
    src/expression.cpp
    src/operator.cpp
    src/scope.cpp
    src/statement.cpp

    src/type.cpp
    src/import.cpp)

target_include_directories(parser PUBLIC include)
target_link_libraries(parser PUBLIC utils fmt hermes)
target_precompile_headers(parser PUBLIC include/parser/kinds.h)
