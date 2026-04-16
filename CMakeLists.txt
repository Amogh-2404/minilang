cmake_minimum_required(VERSION 3.20)
# C is required by LLVM's FindLibEdit.cmake (try_compile of a .c file).
project(minilang LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if the user didn't pick one. This is the convention I
# stole from the LLVM project itself.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Sanitiser switch (-DML_SAN=address|undefined|thread). Off by default; CI
# uses it on Debug builds occasionally.
if(ML_SAN)
    add_compile_options(-fsanitize=${ML_SAN} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${ML_SAN})
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

find_package(LLVM REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR EQUAL 18)
    message(FATAL_ERROR
        "minilang targets LLVM 18 (found ${LLVM_PACKAGE_VERSION}). "
        "Pass -DLLVM_DIR=/path/to/llvm@18/lib/cmake/llvm to override.")
endif()
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Map component names to their concrete library targets. We pull in:
#   core           -- IR, types, constants, basic blocks
#   orcjit         -- ORC v2 JIT
#   native support -- target init helpers
#   passes         -- new pass manager core
#   scalaropts     -- mem2reg, instcombine, etc.
#   transformutils -- LoopSimplify, Mem2Reg utility
#   analysis       -- LoopAnalysis, DominatorTree
#   irreader       -- (not strictly needed; nice for tooling)
#   instcombine    -- pulled in by -O2 anyway, but explicit is better
llvm_map_components_to_libnames(llvm_libs
    core orcjit native support
    passes scalaropts transformutils analysis
    irreader instcombine)

add_executable(minilang
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/codegen.cpp
    src/passes/ConstFoldPass.cpp
    src/passes/DCEPass.cpp
    src/passes/LICMPass.cpp)

target_include_directories(minilang PRIVATE include)
target_link_libraries(minilang PRIVATE ${llvm_libs})

# LLVM was probably built with -fno-rtti and -fno-exceptions; matching it
# avoids subtle ODR/symbol issues across the boundary.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(minilang PRIVATE -fno-rtti)
endif()

# ---------- Unit tests (optional; only if GTest is available) ----------

find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_executable(minilang_unit_tests
        tests/unit/lexer_test.cpp
        tests/unit/parser_test.cpp
        tests/unit/codegen_test.cpp
        src/lexer.cpp
        src/parser.cpp
        src/codegen.cpp
        src/passes/ConstFoldPass.cpp
        src/passes/DCEPass.cpp
        src/passes/LICMPass.cpp)
    target_include_directories(minilang_unit_tests PRIVATE include)
    target_link_libraries(minilang_unit_tests PRIVATE
        ${llvm_libs} GTest::gtest GTest::gtest_main)
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(minilang_unit_tests PRIVATE -fno-rtti)
    endif()
    add_test(NAME unit COMMAND minilang_unit_tests)
endif()
