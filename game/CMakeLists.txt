cmake_minimum_required(VERSION 3.21)
project(worms3d_recomp CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PS2RECOMP_DIR "$ENV{HOME}/src/PS2Recomp" CACHE PATH "PS2Recomp source checkout")
set(WORMS3D_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(WORMS3D_GENERATED_DIR "${WORMS3D_ROOT}/generated")

# Only the runtime (and the IOP library it links) is needed from PS2Recomp.
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL keeps PS2Recomp's placeholder ps2EntryRunner out of the default build;
# ps2_runtime is still built because worms3d links it.
add_subdirectory("${PS2RECOMP_DIR}" ps2recomp EXCLUDE_FROM_ALL)

file(GLOB WORMS3D_GENERATED_SRC CONFIGURE_DEPENDS "${WORMS3D_GENERATED_DIR}/*.cpp")
file(GLOB WORMS3D_OVERRIDE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/overrides/*.cpp")

add_executable(worms3d
    ${WORMS3D_GENERATED_SRC}
    ${WORMS3D_OVERRIDE_SRC}
    "${PS2RECOMP_DIR}/ps2xRuntime/src/main.cpp"
)

target_include_directories(worms3d PRIVATE "${WORMS3D_GENERATED_DIR}")
target_link_libraries(worms3d PRIVATE ps2_runtime)
# Boot the disc copy: the runtime maps cdrom0: to the boot ELF's directory.
# Scoped to main.cpp so changing it doesn't rebuild the generated code.
set_source_files_properties("${PS2RECOMP_DIR}/ps2xRuntime/src/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "PS2X_DEFAULT_BOOT_ELF=\"${WORMS3D_ROOT}/iso/SLUS_208.94\""
)
# Generated MMI code uses SSSE3/SSE4.1 intrinsics; GCC rejects them at the default x86-64 baseline.
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(worms3d PRIVATE -march=x86-64-v2)
endif()

# 93k generated translation units: batch them or the build is dominated by header parsing.
set_target_properties(worms3d PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 64
)
target_precompile_headers(worms3d PRIVATE
    "${PS2RECOMP_DIR}/ps2xRuntime/include/ps2_runtime_macros.h"
    "${PS2RECOMP_DIR}/ps2xRuntime/include/ps2_runtime.h"
    "${WORMS3D_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${WORMS3D_GENERATED_DIR}/ps2_recompiled_stubs.h"
    "${PS2RECOMP_DIR}/ps2xRuntime/include/ps2_syscalls.h"
    "${PS2RECOMP_DIR}/ps2xRuntime/include/ps2_stubs.h"
)
# The overrides and main.cpp are hand-written; keep them out of unity batches.
set_source_files_properties(${WORMS3D_OVERRIDE_SRC} "${PS2RECOMP_DIR}/ps2xRuntime/src/main.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
