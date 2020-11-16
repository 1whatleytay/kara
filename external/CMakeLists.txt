set(FMT_DOC OFF)
set(FMT_INSTALL OFF)
set(FMT_TEST OFF)

set(CLI11_INSTALL OFF)
set(CLI11_BUILD_TESTS OFF)
set(CLI11_BUILD_EXAMPLES OFF)

# CLI11 annoyingly adds CTest targets...
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

add_subdirectory(fmt)
add_subdirectory(CLI11)
add_subdirectory(hermes)

find_package(LLVM REQUIRED CONFIG)

add_library(llvm INTERFACE)
target_link_libraries(llvm INTERFACE
    LLVMMC
    LLVMCore
    LLVMLinker
    LLVMIRReader
    LLVMX86CodeGen
    LLVMX86AsmParser
    LLVMX86Disassembler
    LLVMWebAssemblyCodeGen)
target_compile_definitions(llvm INTERFACE ${LLVM_DEFINITIONS})
target_include_directories(llvm INTERFACE ${LLVM_INCLUDE_DIRS})
