cmake_minimum_required(VERSION 3.20)
project(Quadaeria VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# turn this off if the exe crashes on another pc
option(QC_NATIVE "Optimise for this machine's CPU (-march=native)" ON)
option(QC_PORTABLE "Target generic x86-64-v2 (for binaries you share)" OFF)
option(QC_LTO "Link-time optimisation" ON)

set(_qc_wayland_default OFF)
if(UNIX AND NOT APPLE)
    find_program(_qc_wayland_scanner wayland-scanner)
    find_package(PkgConfig QUIET)
    if(_qc_wayland_scanner AND PKG_CONFIG_FOUND)
        pkg_check_modules(_qc_wl QUIET wayland-client wayland-cursor wayland-egl xkbcommon)
        if(_qc_wl_FOUND)
            set(_qc_wayland_default ON)
        endif()
    endif()
endif()
option(QC_WAYLAND "Native Wayland support (Linux)" ${_qc_wayland_default})

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND ${QC_WAYLAND} CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
    FetchContent_Declare(glfw SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
else()
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio")
    FetchContent_Declare(miniaudio SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio" SOURCE_SUBDIR _none_)
else()
    FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.25 GIT_SHALLOW TRUE SOURCE_SUBDIR _none_)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb")
    FetchContent_Declare(stb SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb" SOURCE_SUBDIR _none_)
else()
    FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20 SOURCE_SUBDIR _none_)
endif()
FetchContent_MakeAvailable(glfw miniaudio stb)

add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(miniaudio PUBLIC MA_NO_ENCODING)
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()

if(QC_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _qc_ipo OUTPUT _ipo_msg)
    if(_qc_ipo)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
endif()

find_package(Threads REQUIRED)

add_library(flecs STATIC vendor/flecs/flecs.c)
target_include_directories(flecs SYSTEM PUBLIC vendor/flecs)
set_target_properties(flecs PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(UNIX)
    target_link_libraries(flecs PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

add_library(qc_options INTERFACE)
target_include_directories(qc_options INTERFACE src)
if(MSVC)
    target_compile_options(qc_options INTERFACE /W3 /permissive- $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(qc_options INTERFACE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
        $<$<CONFIG:Release>:-O3 -fno-math-errno>)
    if(QC_PORTABLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(qc_options INTERFACE -march=x86-64-v2 -mtune=generic)
        target_link_options(qc_options INTERFACE -static-libstdc++ -static-libgcc)
    elseif(QC_NATIVE)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-march=native _qc_native)
        if(_qc_native)
            target_compile_options(qc_options INTERFACE -march=native)
        endif()
    endif()
endif()

add_library(qc_world STATIC
    src/core/Assets.cpp src/world/Block.cpp src/world/Collide.cpp src/world/Gen.cpp src/world/GenLandmarks.cpp src/world/Inventory.cpp
    src/world/Light.cpp src/world/Mesher.cpp src/world/Save.cpp src/world/World.cpp)
target_link_libraries(qc_world PUBLIC qc_options Threads::Threads)
target_compile_definitions(qc_world PRIVATE QC_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

add_library(qc_game STATIC
    src/gfx/GL.cpp src/gfx/Renderer.cpp src/gfx/Textures.cpp src/gfx/UI.cpp
    src/platform/Audio.cpp src/platform/Input.cpp
    src/player/Player.cpp src/player/HeldItem.cpp
    src/sim/Sim.cpp src/sim/Pipeline.cpp src/sim/Objects.cpp src/sim/Bodies.cpp src/sim/Fluids.cpp src/sim/Stress.cpp
    src/sim/Power.cpp src/sim/Leaves.cpp src/sim/Draw.cpp src/sim/DeviceModels.cpp src/sim/Models.cpp
    src/stream/Streamer.cpp src/stream/LightVolume.cpp
    src/app/Game.cpp src/app/GameUI.cpp src/app/GameInventory.cpp src/app/GameRender.cpp src/app/GameInteract.cpp
    src/app/GameAmbient.cpp src/app/Adventure.cpp src/app/Sounds.cpp)
target_include_directories(qc_game SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(qc_game PUBLIC qc_world glfw miniaudio flecs)

add_executable(quadaeria src/app/main.cpp)
target_link_libraries(quadaeria PRIVATE qc_game)
add_custom_command(TARGET quadaeria POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:quadaeria>/assets)
