# Jubelle: gain calibration measurements (playback block, P1; 15 Nov 2026).
# Renders every preset of the built-in soundfont offline with MuseScore's own
# FluidSynth (the copy in src/framework/audio/thirdparty) and reports its level.
cmake_minimum_required(VERSION 3.16)
project(jubelle_gain LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${ROOT}/src/framework/audio/thirdparty/fluidsynth/fluidsynth.cmake)
list(FILTER FLUIDSYNTH_SRC INCLUDE REGEX "\\.(c|cpp)$")
add_executable(jubelle_gain
    ${FLUIDSYNTH_SRC}
    vorbis_shim.c
    ${ROOT}/src/framework/audio/thirdparty/stb/stb_vorbis.c
    measure.cpp)
target_include_directories(jubelle_gain PRIVATE ${FLUIDSYNTH_INC})
target_compile_definitions(jubelle_gain PRIVATE ${FLUIDSYNTH_DEF} EXTERN_VORBIS_SUPPORT ROOT_DIR="${ROOT}")
target_link_libraries(jubelle_gain PRIVATE m)
