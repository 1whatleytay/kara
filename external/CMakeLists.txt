set(BUILD_TESTS OFF CACHE BOOL "Disabled")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Disabled")

set(FMT_DOC OFF CACHE BOOL "Disabled")
set(FMT_INSTALL OFF CACHE BOOL "Disabled")
set(FMT_TEST OFF CACHE BOOL "Disabled")

set(CLI11_INSTALL OFF CACHE BOOL "Disabled")
set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disabled")
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "Disabled")

set(URIPARSER_BUILD_DOCS OFF CACHE BOOL "Disabled")
set(URIPARSER_BUILD_TESTS OFF CACHE BOOL "Disabled")
set(URIPARSER_BUILD_TOOLS OFF CACHE BOOL "Disabled")
set(URIPARSER_ENABLE_INSTALL OFF CACHE BOOL "Disabled")
set(URIPARSER_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disabled")

# CLI11 annoyingly adds CTest targets...
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

add_subdirectory(fmt)
add_subdirectory(CLI11)
add_subdirectory(hermes)
add_subdirectory(pugixml)
add_subdirectory(yaml-cpp)
add_subdirectory(uriparser)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)

add_library(llvm INTERFACE)
target_link_libraries(llvm INTERFACE
    LLVMMC
    LLVMCore
    LLVMOrcJIT
    LLVMLinker
    LLVMIRReader
    LLVMX86CodeGen
    LLVMX86AsmParser
    LLVMX86Disassembler)
target_compile_definitions(llvm INTERFACE ${LLVM_DEFINITIONS})
target_include_directories(llvm INTERFACE ${LLVM_INCLUDE_DIRS})

add_library(clang-global INTERFACE)
target_link_libraries(clang-global INTERFACE clangTooling)
target_compile_definitions(clang-global INTERFACE ${CLANG_DEFINITIONS})
target_include_directories(clang-global INTERFACE ${CLANG_INCLUDE_DIRS})

add_library(lld-global INTERFACE)
target_link_libraries(lld-global INTERFACE
    lldMachO lldCOFF lldELF lldMinGW lldWasm lldCommon)
target_include_directories(lld-global INTERFACE ${LLD_INCLUDE_DIRS})
