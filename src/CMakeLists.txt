add_subdirectory(cli)
add_subdirectory(utils)
add_subdirectory(parser)
add_subdirectory(builder)
add_subdirectory(options)
add_subdirectory(interfaces)

add_executable(kara main.cpp)
target_link_libraries(kara PRIVATE fmt parser options builder)
