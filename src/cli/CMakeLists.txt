add_executable(cli
    include/cli/cbp.h
    include/cli/cli.h
    include/cli/config.h
    include/cli/manager.h
    include/cli/packages.h
    include/cli/log.h

    src/actions/add.cpp
    src/actions/build.cpp
    src/actions/clean.cpp
    src/actions/remove.cpp
    src/actions/run.cpp
    src/actions/create.cpp
    src/actions/compile.cpp

    src/cbp.cpp
    src/cli.cpp
    src/config.cpp
    src/manager.cpp
    src/log.cpp
    src/packages.cpp
    src/main.cpp)
target_include_directories(cli PUBLIC include)
target_link_libraries(cli PRIVATE CLI11 yaml-cpp builder fmt lld-global pugixml-static uriparser)