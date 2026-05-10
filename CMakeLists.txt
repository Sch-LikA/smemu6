cmake_minimum_required(VERSION 3.16)
project(smemu6 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── SDL2 ─────────────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)

# ── redcode/Zeta (header-only; required by redcode/Z80) ───────────────────────
include(FetchContent)
FetchContent_Declare(
    Zeta
    GIT_REPOSITORY https://github.com/redcode/Zeta.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(Zeta)

# ── redcode/Z80 (CPU core) ────────────────────────────────────────────────────
include(FetchContent)
include(FetchContent)
FetchContent_Declare(
    Z80
    GIT_REPOSITORY https://github.com/redcode/Z80.git
    GIT_TAG        master
)
# Build only the library, not examples/tests
set(Z80_WITH_EXECUTE     ON  CACHE BOOL "" FORCE)
set(Z80_WITH_FULL_IM0    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Z80)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(smemu6
    src/main.c
    src/machine.c
    src/memory.c
    src/video.c
    src/keyboard.c
    src/usart.c
    src/parallel.c
    src/floppy.c
    src/sound.c
    src/debug.c
    src/rtc.c
    src/winchester.c
)

target_include_directories(smemu6 PRIVATE src)

target_link_libraries(smemu6 PRIVATE
    SDL2::SDL2
    Z80
    m
)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(smemu6 PRIVATE /W3)
else()
    target_compile_options(smemu6 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── ROM directory ─────────────────────────────────────────────────────────────
# Copy ROMs to build directory so the emulator can find them at runtime
if(EXISTS ${CMAKE_SOURCE_DIR}/roms)
    file(COPY ${CMAKE_SOURCE_DIR}/roms DESTINATION ${CMAKE_BINARY_DIR})
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/floppies)
    file(COPY ${CMAKE_SOURCE_DIR}/floppies DESTINATION ${CMAKE_BINARY_DIR})
endif()
