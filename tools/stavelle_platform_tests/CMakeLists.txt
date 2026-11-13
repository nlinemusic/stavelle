# Stavelle: fast, Qt-free reliability tests that run natively on Windows,
# macOS and Linux in a few minutes (the full application build takes ~1 hour).
cmake_minimum_required(VERSION 3.16)
project(stavelle_platform_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Use the same GoogleTest that ships with the MuseScore sources.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/src/framework/testing/thirdparty/googletest googletest EXCLUDE_FROM_ALL)

add_executable(stavelle_platform_tests
    ${ROOT}/src/project/internal/safefilewrite.cpp
    ${ROOT}/src/project/internal/safefilewrite.h
    ${ROOT}/src/project/tests/safefilewrite_tests.cpp
)
target_include_directories(stavelle_platform_tests PRIVATE ${ROOT}/src)
target_link_libraries(stavelle_platform_tests PRIVATE gtest_main)

if (MSVC)
    target_compile_options(stavelle_platform_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(stavelle_platform_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME stavelle_platform_tests COMMAND stavelle_platform_tests)
