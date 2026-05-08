# MinGW-w64 cross-compilation toolchain (Linux host -> Windows x86_64 target).
# Used by scripts/build-windows-mingw-from-linux.sh
#
# Requires the `mingw-w64` package (Debian/Ubuntu/Fedora) which provides
# x86_64-w64-mingw32-gcc / -g++ / -windres.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static link the C++ runtime so the resulting .exe has no MinGW DLL deps.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
