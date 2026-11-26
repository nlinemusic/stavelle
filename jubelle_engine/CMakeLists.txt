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
    model/detailskey.cpp
    model/fragment.cpp
    perform/repeatlist.cpp
    model/detailskey.h
    edit/changeset.cpp
    edit/changeset.h
    edit/history.cpp
    edit/history.h
    edit/structure.cpp
    edit/operations.cpp
    edit/commands.cpp
    edit/chordsymbol.cpp
    edit/chordsymbol.h
    edit/commands.h
    edit/noteinput.cpp
    edit/clipboard.cpp
    edit/tablature.cpp
    edit/links.cpp
    edit/structure.h
    edit/scorebuilder.cpp
    edit/scorebuilder.h
)
target_include_directories(je_core PUBLIC ${JE_ROOT})
target_link_libraries(je_core PUBLIC Threads::Threads)

# ---- je::io: MuseScore 4.x files (S2). zlib (zlib licence) for the archives: the system's, or fetched
# from zlib's official repository when the system has none (Windows CI)
find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    include(FetchContent)
    FetchContent_Declare(zlib GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()
add_library(je_io STATIC
    io/xml.cpp
    io/xml.h
    io/zip.cpp
    io/zip.h
    io/scorefile.cpp
    io/scorefile.h
    io/details.cpp
    io/mscxwriter.cpp
    io/details.h
    io/mscxreader.cpp
    io/mscxreader.h
)
target_include_directories(je_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(je_io PUBLIC je_core PRIVATE ZLIB::ZLIB)

# the corpus tool: round trips and reading checks over a folder of scores (used by CI and locally)
add_executable(je_io_tool tools/je_io_tool.cpp)
target_link_libraries(je_io_tool PRIVATE je_io)

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
    tests/notation_tests.cpp
    tests/io_tests.cpp
    tests/io_reader_regression_tests.cpp
    tests/io_writer_tests.cpp
    tests/edit_operations_tests.cpp
    tests/edit_commands_tests.cpp
    tests/perform_repeatlist_tests.cpp
    tests/edit_noteinput_tests.cpp
    tests/edit_clipboard_tests.cpp
    tests/edit_links_tests.cpp
)
target_compile_definitions(je_tests PRIVATE JE_REPO_DIR="${JE_REPO}")
target_link_libraries(je_tests PRIVATE je_core je_io gtest)

add_executable(je_bench bench/main.cpp)
target_link_libraries(je_bench PRIVATE je_core)

enable_testing()
add_test(NAME je_tests COMMAND je_tests)
