add_library(interfaces STATIC
    include/interfaces/header.h
    include/interfaces/interfaces.h

    src/header.cpp)

target_include_directories(interfaces PUBLIC include)
target_link_libraries(interfaces PRIVATE clang-global llvm fmt)
target_link_libraries(interfaces PUBLIC parser)

add_executable(try interfaces.cpp)
target_link_libraries(try PRIVATE interfaces)
