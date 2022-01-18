add_executable(cli
    include/cli/cbp.h
    include/cli/cli.h
    include/cli/config.h
    include/cli/manager.h
    include/cli/packages.h
    include/cli/exposer.h
    include/cli/log.h
    include/cli/lock.h
    include/cli/utility.h
    include/cli/platform.h

    src/actions/add.cpp
    src/actions/build.cpp
    src/actions/clean.cpp
    src/actions/remove.cpp
    src/actions/run.cpp
    src/actions/create.cpp
    src/actions/compile.cpp
    src/actions/expose.cpp

    src/cbp.cpp
    src/cli.cpp
    src/exposer.cpp
    src/config.cpp
    src/manager.cpp
    src/log.cpp
    src/packages.cpp
    src/platform.cpp
    src/lock.cpp
    src/utility.cpp
    src/main.cpp)
target_include_directories(cli PUBLIC include)
target_link_libraries(cli PRIVATE
    fmt utils builder parser interfaces lld-global pugixml-static yaml-cpp uriparser CLI11)