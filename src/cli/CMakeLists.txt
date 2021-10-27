add_executable(cli
    src/main.cpp)
target_include_directories(cli PUBLIC include)
target_link_libraries(cli PRIVATE CLI11 yaml-cpp builder fmt lld-global)