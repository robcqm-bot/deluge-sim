# =====================================================================
# deluge-sim — build (CMake >= 3.15, C++17)
# Targets: deluge_sim_lib | suite_core | board_core | simcli | simtests
#          | board_sim (opcional, requiere SDL2)
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

# ---- núcleo del simulador (zona deluge_sim) ----
add_library(deluge_sim_lib STATIC
    src/wav_io.cpp
    src/engine.cpp
    src/stream_source.cpp
    src/preset.cpp
)
target_include_directories(deluge_sim_lib PUBLIC include)

# ---- zona propia del cortafuegos (suite_core) ----
add_library(suite_core STATIC
    suite_core/src/fet76_ref.cpp
    suite_core/src/opto_comp.cpp
    suite_core/src/passive_eq.cpp
    suite_core/src/plate_reverb.cpp
    suite_core/src/tape_sat.cpp
)
target_include_directories(suite_core PUBLIC suite_core/include)
# NOTA DE ARQUITECTURA: suite_core NO enlaza contra deluge_sim_lib.
# La dependencia va en una sola dirección: app -> ambas.

# ---- CLI ----
add_executable(simcli app/main.cpp)
target_link_libraries(simcli PRIVATE deluge_sim_lib suite_core)

# ---- núcleo del board (fase 2, SDL-free) ----
# Capa app: conoce deluge_sim pero NUNCA incluye SDL (el shell gráfico
# board_sim es la única pieza que enlaza SDL2).
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

# ---- board simulator SDL2 (opcional; fase 2 v0.2) ----
# Rutas aceptadas:
#   -SDL2_SDK_DIR=<...>/x86_64-w64-mingw32   (SDK oficial de SDL para MinGW;
#        también via variable de entorno SDL2_SDK)
#   - find_package(SDL2) (Linux: libsdl2-dev; CI Ubuntu)
# Si no hay SDL2 el target se omite con un aviso y el resto sigue igual.
set(SDL2_SDK_DIR "$ENV{SDL2_SDK}" CACHE PATH "Ruta SDK SDL2 (subcarpeta del triplet)")
find_package(SDL2 QUIET)

# Definición común del target (la variante solo cambia cómo se enlaza SDL).
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
