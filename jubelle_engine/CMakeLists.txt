# SPDX-License-Identifier: GPL-3.0-only
# Jubelle engine: standalone build (plain C++17, no Qt). Phase 3 foundations.
#
#   cmake -S jubelle_engine -B build-je -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-je --parallel
#   ctest --test-dir build-je --output-on-failure
#
# Nothing here is part of the Jubelle application build.
cmake_minimum_required(VERSION 3.16)
project(jubelle_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (not MSVC)" OFF)
option(JE_TSAN "Build with ThreadSanitizer (not MSVC)" OFF)

set(JE_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(JE_REPO ${JE_ROOT}/.. ABSOLUTE)

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if (JE_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
    if (JE_TSAN)
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()
endif()

find_package(Threads REQUIRED)

add_library(je_core STATIC
    exec/threadpool.cpp
    exec/threadpool.h
    model/fraction.cpp
    model/fraction.h
    model/id.cpp
    model/id.h
    model/cowvector.h
    model/value.h
    model/idmap.h
    model/fields.h
    model/object.cpp
    model/object.h
    model/model.cpp
    model/model.h
    model/bulkbuilder.cpp
    model/bulkbuilder.h
    model/invariants.cpp
    model/invariants.h
    edit/changeset.cpp
    edit/changeset.h
    edit/history.cpp
    edit/history.h
    edit/structure.cpp
    edit/structure.h
    edit/scorebuilder.cpp
    edit/scorebuilder.h
)
target_include_directories(je_core PUBLIC ${JE_ROOT})
target_link_libraries(je_core PUBLIC Threads::Threads)

# Tests (GoogleTest from the repository's framework, as Jubelle's other test tools use)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${JE_REPO}/src/framework/testing/thirdparty/googletest googletest EXCLUDE_FROM_ALL)

add_executable(je_tests
    tests/main.cpp
    tests/corpus.cpp
    tests/corpus.h
    tests/threadpool_tests.cpp
    tests/fraction_tests.cpp
    tests/id_tests.cpp
    tests/cowvector_tests.cpp
    tests/changeset_tests.cpp
    tests/corpus_tests.cpp
    tests/idmap_tests.cpp
    tests/model_tests.cpp
    tests/bulkbuilder_tests.cpp
)
target_compile_definitions(je_tests PRIVATE JE_REPO_DIR="${JE_REPO}")
target_link_libraries(je_tests PRIVATE je_core gtest)

add_executable(je_bench bench/main.cpp)
target_link_libraries(je_bench PRIVATE je_core)

enable_testing()
add_test(NAME je_tests COMMAND je_tests)
