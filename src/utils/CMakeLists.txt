add_library(utils STATIC
    include/utils/literals.h
    include/utils/typename.h
    include/utils/expression.h

    src/typename.cpp
    src/expression.cpp)

target_include_directories(utils PUBLIC include)
target_link_libraries(utils PRIVATE fmt)
