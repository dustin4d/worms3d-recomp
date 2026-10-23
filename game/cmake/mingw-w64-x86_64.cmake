# Cross-compile the runner for Windows x64 from WSL/Arch with mingw-w64 (pacman: mingw-w64-gcc).
#   cmake -S game -B game/build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/game/cmake/mingw-w64-x86_64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${MINGW_TRIPLE}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Self-contained .exe: no libstdc++/libgcc/winpthread DLLs to ship next to it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
