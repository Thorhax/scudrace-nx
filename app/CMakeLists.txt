# Scud Race (wanszai's Xbox 360 build): static recompilation host application.
#
# The game code is not in this repository: tools/codegen.sh writes app/generated from default.xex (the X360 release)
# together with the CMake glue (generated/rexglue.cmake, made by `rexglue codegen`).
#
# Stage 3, PC (Linux):  cmake --preset linux-pc -S app && cmake --build out/pc
#
# CMakeLists.mk9-reference.txt is mk9-nx's file, kept for the stages still to come (Renut on deko3d, the Switch NRO).

cmake_minimum_required(VERSION 3.25)
project(scud VERSION 1.0.0 LANGUAGES CXX)
set(SCUD_VERSION "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "Path to the ReXGlue SDK source tree")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    message(FATAL_ERROR "app/generated/rexglue.cmake is missing: run tools/codegen.sh first")
endif()
include(generated/rexglue.cmake)

# The build re-runs the generator whenever an input changes; tools/post_codegen.py (tools/codegen.sh's last step:
# the flavour's passes and Renut's store cards) is appended to that same command, so a rebuild never drops them (as mk9-nx's post_codegen).
if(TARGET scud_codegen)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp" APPEND
        COMMAND "${CMAKE_COMMAND}" -E env "REXSDK_DIR=${REXSDK_DIR}"
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/post_codegen.py")
endif()


set(SCUD_SOURCES
    src/scud_main.cpp
    # Linux: fatal host signals the SDK does not report (illegal instruction, ...) with the executable offset.
    src/scud_crash_log.cpp
    # Scripted gamepad on player 1 for automated tests (cvars scud_input_script, scud_input_script_file).
    src/scud_script_input.cpp
    # Presented frames per second in the log (hook on D3DDevice_Swap).
    src/scud_frame_counter.cpp
    # Still snapshots of the game image for testing (cvar scud_snapshot_s).
    src/scud_snapshot.cpp
    # Shader containers for Renut's library (cvar scud_dump_shaders).
    src/scud_shader_dump.cpp
    # The game's environment options and its stdout / stderr (cvars scud_env, scud_game_output).
    src/scud_game_env.cpp
    src/scud_cheats.cpp
    src/scud_boot_timing.cpp
    src/scud_fast_boot.cpp
    src/scud_native_math.cpp
    src/scud_dynamic_res.cpp
    src/scud_pgo.cpp
)
# Renut's Direct3D-level renderer (src/renut_engine, from mk9-nx; retargeted by tools/scud_renut_retarget.py,
# docs/scud-renut-census.md): deko3d on the Switch, Vulkan (plume) on the PC. sa2_renderer = "renut" chooses it.
option(SCUD_RENUT_RENDERER "Link Renut's Direct3D-level renderer (sa2_renderer = renut)" ON)
if(SCUD_RENUT_RENDERER)
    list(APPEND SCUD_SOURCES
        # What Renut calls outside the renderer: the switchable enhancements, the frame recorder, MK9-only stubs.
        src/cpu/sa2_enhancements.cpp
        src/cpu/sa2_gpu_boost.cpp
        src/sa2_capture.cpp
        src/scud_renut_glue.cpp)
    if(NOT REXGLUE_PLATFORM_SWITCH)
        list(APPEND SCUD_SOURCES src/me/me_pc_stubs.cpp src/profiler/sa2_cpu_profile_pc.cpp)
    endif()
endif()

set(SCUD_EXTRA_SOURCES "" CACHE STRING "Extra source files for diagnostic builds (empty for normal builds)")
if(SCUD_EXTRA_SOURCES)
    list(APPEND SCUD_SOURCES ${SCUD_EXTRA_SOURCES})
endif()

add_executable(scud ${SCUD_SOURCES})
target_include_directories(scud PRIVATE src "${REXSDK_DIR}/thirdparty/xxHash")

# Stage 3 draws with ReXGlue's Xenos GPU emulation.
rexglue_setup_target(scud GPU_PLUGINS xenos)

# The Thorhax intro (app/thorhax_intro, as in mk9-nx): the startup video, thorhax::intro::Start() in ScudApp::OnPreSetup.
include("${CMAKE_CURRENT_SOURCE_DIR}/thorhax_intro/thorhax_intro.cmake")
thorhax_intro_add(scud)

if(SCUD_RENUT_RENDERER)
    target_include_directories(scud PRIVATE src/me "${REXSDK_DIR}/src/core")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/scud_renut_gpu.cmake")
endif()

if(REXGLUE_PLATFORM_SWITCH AND TARGET rexcore)
    target_compile_definitions(rexcore PRIVATE REX_SWITCH_FALLBACK_EXE="/switch/scudrace-nx/scudrace-nx.nro")
endif()

# Fused multiply-add from separate PPC fmul/fadd drifts from the Xbox 360's results, and strict aliasing is unsafe for
# recompiled code (as in sa2-nx / mk9-nx).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target scud scud_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off -fno-strict-aliasing)
        endif()
    endforeach()
endif()

if(REXGLUE_PLATFORM_SWITCH)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/scud_register.cpp" APPEND PROPERTY
                 COMPILE_OPTIONS -O0)
    target_compile_options(scud PRIVATE -g1 -gdwarf-4)
    # Link-time optimisation (mk9-nx / sa2-nx's set; renut-nx docs/pgo-lto.md): the game code, the app and Renut's
    # renderer. Off by default (the LTO link is slow); the release folder out/nx-dk-lto turns it on.
    option(SCUD_SWITCH_LTO "Link-time optimisation of the game code, the app and the renderer (Switch)" OFF)
    set(SCUD_SWITCH_LTO_JOBS "6" CACHE STRING "Parallel LTRANS jobs of the Switch LTO link")
    if(SCUD_SWITCH_LTO)
        foreach(target scud_recomp scud scud_renut_gpu plume)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -flto -fno-fat-lto-objects -fipa-pta)
                target_compile_definitions(${target} PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
            endif()
        endforeach()
        target_link_options(scud PRIVATE -flto=${SCUD_SWITCH_LTO_JOBS} -flto-partition=balanced -fipa-pta
                            -ffp-contract=off -fno-strict-aliasing)
        message(STATUS "scud: Switch LTO on (${SCUD_SWITCH_LTO_JOBS} LTRANS jobs)")
    endif()
    # Profile-guided optimisation (renut-nx docs/pgo-lto.md, from sa2-nx): SCUD_PGO=generate builds an NRO that streams
    # its branch counts to pgo/ next to it (src/scud_pgo.cpp); tools/pgo_merge.py makes .gcda files of them in
    # SCUD_PGO_DIR; SCUD_PGO=use reads them (with LTO: out/nx-dk-pgo-use).
    set(SCUD_PGO "" CACHE STRING "Profile-guided optimisation of the Switch build: empty, generate or use")
    set(SCUD_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
    get_filename_component(_scud_pgo_dir "${SCUD_PGO_DIR}" ABSOLUTE)
    if(SCUD_PGO STREQUAL "generate")
        set(_scud_pgo "-fprofile-generate=${_scud_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                      -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(scud PRIVATE SCUD_PGO_GENERATE=1)
    elseif(SCUD_PGO STREQUAL "use")
        if(NOT EXISTS "${_scud_pgo_dir}")
            message(FATAL_ERROR "SCUD_PGO=use: no profile in ${_scud_pgo_dir}")
        endif()
        set(_scud_pgo "-fprofile-use=${_scud_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                      -fprofile-correction -fno-profile-values -fprofile-partial-training -Wno-missing-profile
                      -Wno-coverage-mismatch)
        if(TARGET scud_recomp)
            target_compile_options(scud_recomp PRIVATE --param=max-inline-insns-auto=64 --param=inline-unit-growth=12)
        endif()
    elseif(SCUD_PGO)
        message(FATAL_ERROR "SCUD_PGO must be empty, generate or use (got ${SCUD_PGO})")
    endif()
    if(SCUD_PGO)
        foreach(target scud_recomp scud scud_renut_gpu plume)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_scud_pgo})
            endif()
        endforeach()
        target_link_options(scud PRIVATE ${_scud_pgo})
        message(STATUS "scud: PGO ${SCUD_PGO} (${_scud_pgo_dir})")
    endif()
    # Fixed settings compiled in (Renut on deko3d, ...) and the player's scud.toml written next to the NRO.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scud_switch_defaults.toml" SCUD_BUILTIN_CONFIG)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scud_switch.toml" SCUD_PLAYER_CONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/scud_switch_defaults.toml" "${CMAKE_CURRENT_SOURCE_DIR}/scud_switch.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/scud_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/scud_builtin_config.h" @ONLY)
    target_include_directories(scud PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(scud PRIVATE SCUD_BUILTIN_CONFIG=1)
    target_link_options(scud PRIVATE -g1 -gdwarf-4)
    rexglue_switch_add_nro(scud
        NAME "Scud Race (0.0.17)"
        AUTHOR "Thorhax"
        VERSION "${SCUD_VERSION}"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
endif()
