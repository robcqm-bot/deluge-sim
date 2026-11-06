# =====================================================================
# deluge-sim — build (CMake >= 3.15, C++17, cero dependencias externas)
# Targets: deluge_sim_lib | suite_core | simcli | simtests
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
)
target_include_directories(deluge_sim_lib PUBLIC include)

# ---- zona propia del cortafuegos (suite_core) ----
add_library(suite_core STATIC
    suite_core/src/fet76_ref.cpp
)
target_include_directories(suite_core PUBLIC suite_core/include)
# NOTA DE ARQUITECTURA: suite_core NO enlaza contra deluge_sim_lib.
# La dependencia va en una sola dirección: app -> ambas.

# ---- CLI ----
add_executable(simcli app/main.cpp)
target_link_libraries(simcli PRIVATE deluge_sim_lib suite_core)

# ---- tests ----
enable_testing()
add_executable(simtests tests/test_all.cpp)
target_link_libraries(simtests PRIVATE deluge_sim_lib suite_core)
add_test(NAME all_tests COMMAND simtests)
