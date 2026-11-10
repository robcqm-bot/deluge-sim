# =====================================================================
# deluge-sim — build (CMake >= 3.15, C++17)
# Targets: deluge_sim_lib | suite_core | board_core | simcli | simtests
#          | board_sim (optional, requires SDL2)
# =====================================================================
cmake_minimum_required(VERSION 3.15)
project(deluge_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /O2 /EHsc)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# ---- simulator core (deluge_sim zone) ----
add_library(deluge_sim_lib STATIC
    src/wav_io.cpp
    src/engine.cpp
    src/stream_source.cpp
    src/preset.cpp
    src/q31_bridge.cpp
)
target_include_directories(deluge_sim_lib PUBLIC include)

# ---- firewall own zone (suite_core) ----
add_library(suite_core STATIC
    suite_core/src/fet76_ref.cpp
    suite_core/src/opto_comp.cpp
    suite_core/src/passive_eq.cpp
    suite_core/src/plate_reverb.cpp
    suite_core/src/tape_sat.cpp
)
target_include_directories(suite_core PUBLIC suite_core/include)
# ARCHITECTURE NOTE: suite_core does NOT link against deluge_sim_lib.
# The dependency goes in one direction only: app -> both.

# ---- CLI ----
add_executable(simcli app/main.cpp)
target_link_libraries(simcli PRIVATE deluge_sim_lib suite_core)

# ---- board core (phase 2, SDL-free) ----
# App layer: knows deluge_sim but NEVER includes SDL (the graphical
# shell board_sim is the only piece that links SDL2).
add_library(board_core STATIC
    app/board_sim/board_core.cpp
)
target_include_directories(board_core PUBLIC app/board_sim)
target_link_libraries(board_core PUBLIC deluge_sim_lib)

# ---- tests ----
enable_testing()
add_executable(simtests tests/test_all.cpp)
target_include_directories(simtests PRIVATE app)   # fx_factory.hpp
target_link_libraries(simtests PRIVATE deluge_sim_lib suite_core board_core)
add_test(NAME all_tests COMMAND simtests)

# ---- SDL2 board simulator (optional; phase 2 v0.2) ----
# Accepted paths:
#   -SDL2_SDK_DIR=<...>/x86_64-w64-mingw32   (official SDL SDK for MinGW;
#        also via the SDL2_SDK environment variable)
#   - find_package(SDL2) (Linux: libsdl2-dev; CI Ubuntu)
# If there is no SDL2 the target is skipped with a notice and the rest
# proceeds unchanged.
set(SDL2_SDK_DIR "$ENV{SDL2_SDK}" CACHE PATH "Ruta SDK SDL2 (subcarpeta del triplet)")
find_package(SDL2 QUIET)

# Common target definition (the variant only changes how SDL is linked).
function(_add_board_sim)
    add_executable(board_sim app/board_sim/board_main.cpp)
    target_link_libraries(board_sim PRIVATE deluge_sim_lib suite_core board_core)
    target_include_directories(board_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
endfunction()

if(SDL2_SDK_DIR AND EXISTS "${SDL2_SDK_DIR}/include/SDL2/SDL.h")
    _add_board_sim()
    target_include_directories(board_sim PRIVATE "${SDL2_SDK_DIR}/include")
    target_link_directories(board_sim PRIVATE "${SDL2_SDK_DIR}/lib")
    target_link_libraries(board_sim PRIVATE SDL2)
    if(EXISTS "${SDL2_SDK_DIR}/bin/SDL2.dll")
        add_custom_command(TARGET board_sim POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_SDK_DIR}/bin/SDL2.dll"
                    $<TARGET_FILE_DIR:board_sim>)
    endif()
    message(STATUS "board_sim: usando SDK SDL2 en ${SDL2_SDK_DIR}")
elseif(TARGET SDL2::SDL2)
    _add_board_sim()
    target_link_libraries(board_sim PRIVATE SDL2::SDL2)
    message(STATUS "board_sim: usando SDL2 encontrado por find_package")
elseif(SDL2_FOUND)
    _add_board_sim()
    target_include_directories(board_sim PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(board_sim PRIVATE ${SDL2_LIBRARIES})
    message(STATUS "board_sim: usando SDL2 (variables antiguas de find_package)")
else()
    message(STATUS "SDL2 no encontrado: target 'board_sim' omitido "
                   "(instala libsdl2-dev o pasa -DSDL2_SDK_DIR=...)")
endif()
