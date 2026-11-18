# Jubelle: fast, Qt-free reliability tests that run natively on Windows,
# macOS and Linux in a few minutes (the full application build takes ~1 hour).
cmake_minimum_required(VERSION 3.16)
project(jubelle_platform_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Use the same GoogleTest that ships with the MuseScore sources.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/src/framework/testing/thirdparty/googletest googletest EXCLUDE_FROM_ALL)

add_executable(jubelle_platform_tests
    ${ROOT}/src/project/internal/safefilewrite.cpp
    ${ROOT}/src/project/internal/safefilewrite.h
    ${ROOT}/src/project/tests/safefilewrite_tests.cpp
    ${ROOT}/src/project/internal/projectlibrary.cpp
    ${ROOT}/src/project/internal/projectlibrary.h
    ${ROOT}/src/project/tests/projectlibrary_tests.cpp
    ${ROOT}/src/project/internal/musicxmlsummary.cpp
    ${ROOT}/src/project/internal/musicxmlsummary.h
    ${ROOT}/src/project/tests/jubelle_musicxmlsummary_tests.cpp
    ${ROOT}/src/project/internal/youtubeupload.cpp
    ${ROOT}/src/project/internal/youtubeupload.h
    ${ROOT}/src/project/tests/jubelle_youtubeupload_tests.cpp
    ${ROOT}/src/project/internal/projectinfo.cpp
    ${ROOT}/src/project/internal/projectinfo.h
    ${ROOT}/src/project/tests/projectinfo_tests.cpp
    ${ROOT}/src/project/internal/templatesuggestions.cpp
    ${ROOT}/src/project/internal/templatesuggestions.h
    ${ROOT}/src/project/tests/templatesuggestions_tests.cpp
    ${ROOT}/src/project/internal/usertemplates.cpp
    ${ROOT}/src/project/tests/usertemplates_tests.cpp
    ${ROOT}/src/project/tests/newproject_tests.cpp
    ${ROOT}/src/notation/internal/jubelle/chordidentifier.cpp
    ${ROOT}/src/notation/internal/jubelle/chordidentifier.h
    ${ROOT}/src/notation/tests/chordidentifier_tests.cpp
    ${ROOT}/src/notation/internal/jubelle/condensingplanner.cpp
    ${ROOT}/src/notation/internal/jubelle/condensingplanner.h
    ${ROOT}/src/notation/tests/condensingplanner_tests.cpp
    ${ROOT}/src/notation/internal/jubelle/passagelinkplanner.cpp
    ${ROOT}/src/notation/internal/jubelle/passagelinkplanner.h
    ${ROOT}/src/notation/tests/passagelinkplanner_tests.cpp
    ${ROOT}/src/framework/vst/internal/jubelle/vstpresetfile.cpp
    ${ROOT}/src/framework/vst/internal/jubelle/vstpresetfile.h
    ${ROOT}/src/framework/vst/tests/jubelle_vstpreset_tests.cpp
    ${ROOT}/src/framework/vst/internal/jubelle/vstkeyswitches.cpp
    ${ROOT}/src/framework/vst/internal/jubelle/vstkeyswitches.h
    ${ROOT}/src/framework/vst/tests/jubelle_vstkeyswitches_tests.cpp
    ${ROOT}/src/importexport/videoexport/internal/jubelle/audiosplice.cpp
    ${ROOT}/src/importexport/videoexport/internal/jubelle/audiosplice.h
    ${ROOT}/src/importexport/videoexport/tests/jubelle_audiosplice_tests.cpp
    ${ROOT}/src/appshell/internal/jubelle/performanceprofile.cpp
    ${ROOT}/src/appshell/internal/jubelle/performanceprofile.h
    ${ROOT}/src/appshell/tests/jubelle_performanceprofile_tests.cpp
)
target_include_directories(jubelle_platform_tests PRIVATE ${ROOT}/src)
target_link_libraries(jubelle_platform_tests PRIVATE gtest_main)
if (APPLE)
    target_link_libraries(jubelle_platform_tests PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

if (MSVC)
    target_compile_options(jubelle_platform_tests PRIVATE /W4 /utf-8)
    target_compile_definitions(jubelle_platform_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(jubelle_platform_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME jubelle_platform_tests COMMAND jubelle_platform_tests)

# Jubelle: tests that read real files from the repository (MusicXML test data)
target_compile_definitions(jubelle_platform_tests PRIVATE JUBELLE_SOURCE_ROOT="${ROOT}")
