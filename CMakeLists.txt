cmake_minimum_required(VERSION 3.16)
project(smemu6 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# EM_ASM requires GNU extensions; enable them for Emscripten builds.
if(EMSCRIPTEN)
    set(CMAKE_C_EXTENSIONS ON)
else()
    set(CMAKE_C_EXTENSIONS OFF)
endif()

# ── SDL2 ─────────────────────────────────────────────────────────────────────
# Emscripten provides SDL2 as a built-in port; skip find_package for web builds.
if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
endif()

# ── redcode/Zeta (header-only; required by redcode/Z80) ───────────────────────
include(FetchContent)
FetchContent_Declare(
    Zeta
    GIT_REPOSITORY https://github.com/redcode/Zeta.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(Zeta)

# Tell Z80's FindZeta.cmake where the headers are.
# Under Emscripten (and other cross-compile targets), find_path() won't search
# FetchContent paths automatically, so we set the cache variable explicitly.
set(Zeta_INCLUDE_DIR "${Zeta_SOURCE_DIR}/API" CACHE PATH "Zeta API include dir" FORCE)

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

# ── tinyfiledialogs (native file dialog; no CMake build, just two files) ─────
# Not used for Emscripten builds (no native dialog API in the browser).
if(NOT EMSCRIPTEN)
FetchContent_Declare(
    tinyfiledialogs
    GIT_REPOSITORY https://git.code.sf.net/p/tinyfiledialogs/code
    GIT_TAG        master
)
FetchContent_GetProperties(tinyfiledialogs)
if(NOT tinyfiledialogs_POPULATED)
    FetchContent_Populate(tinyfiledialogs)
endif()
endif() # NOT EMSCRIPTEN

# ── Sources ───────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
set(TINYFD_SOURCES "")
set(TINYFD_INCLUDE "")
else()
set(TINYFD_SOURCES ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c)
set(TINYFD_INCLUDE ${tinyfiledialogs_SOURCE_DIR})
endif()

add_executable(smemu6
    src/main.c
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/launcher.c>
    ${TINYFD_SOURCES}
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

# ── Platform icon resources ────────────────────────────────────────────────────
# Windows: embed .ico via .rc resource file so the .exe has a taskbar/Explorer icon
if(WIN32)
    target_sources(smemu6 PRIVATE "${CMAKE_SOURCE_DIR}/smaky6.rc")
endif()

# macOS: build as .app bundle with .icns icon
if(APPLE)
    set_target_properties(smemu6 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Smaky 6 Emulator"
        MACOSX_BUNDLE_GUI_IDENTIFIER "ch.epsitec.smemu6"
        MACOSX_BUNDLE_ICON_FILE "smaky6.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
    set_source_files_properties("${CMAKE_SOURCE_DIR}/smaky6.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(smemu6 PRIVATE "${CMAKE_SOURCE_DIR}/smaky6.icns")
endif()

target_include_directories(smemu6 PRIVATE src ${TINYFD_INCLUDE})

if(NOT EMSCRIPTEN AND DEFINED SDL2_INCLUDE_DIRS)
    # The SDL2 cmake config's imported target only exports include/SDL2,
    # but sources use <SDL2/SDL.h> so we need the parent include/ too.
    target_include_directories(smemu6 PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

if(EMSCRIPTEN)
    # Emscripten provides SDL2 as a built-in port.
    # We embed the ROMs into the virtual FS.
    # Output .js (not .html) to avoid Emscripten 3.1.6 htmlmin bug;
    # web/index.html loads the generated smemu6.js directly.
    set_target_properties(smemu6 PROPERTIES SUFFIX ".js")

    target_compile_options(smemu6 PRIVATE -s USE_SDL=2)

    # Build link flags as a string (target_link_options mis-parses -s KEY=VAL)
    set(EM_LINK_FLAGS "-s USE_SDL=2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=67108864 -s EXPORTED_RUNTIME_METHODS=[ccall,cwrap,FS] -s EXPORTED_FUNCTIONS=[_main,_smemu6_reset,_smemu6_mount_dx0,_smemu6_mount_dx1] -s MODULARIZE=0 -s FORCE_FILESYSTEM=1")
    # Only preload the Phantom boot ROM; chargen is built into the emulator.
    foreach(_rom samos_sys17.rom)
        if(EXISTS "${CMAKE_SOURCE_DIR}/roms/${_rom}")
            string(APPEND EM_LINK_FLAGS " --preload-file \"${CMAKE_SOURCE_DIR}/roms/${_rom}@/roms/${_rom}\"")
        endif()
    endforeach()
    # Floppies and hard-disk images are NOT preloaded; the user uploads them
    # via the browser UI and they are written into the virtual FS at runtime.
    set_target_properties(smemu6 PROPERTIES LINK_FLAGS "${EM_LINK_FLAGS}")

    # Copy web/index.html into the build directory after linking.
    add_custom_command(TARGET smemu6 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/index.html"
            "${CMAKE_BINARY_DIR}/index.html"
        COMMENT "Copying web/index.html to build directory"
    )

    target_link_libraries(smemu6 PRIVATE Z80 m)
else()
    # SDL2main provides WinMain→SDL_main on Windows; harmless on other platforms.
    if(TARGET SDL2::SDL2main)
        target_link_libraries(smemu6 PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(smemu6 PRIVATE
        SDL2::SDL2
        Z80
        m
    )
endif()

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

# ── Install rules (used by AppImage packaging) ────────────────────────────────
if(NOT EMSCRIPTEN)
    include(GNUInstallDirs)
    install(TARGETS smemu6 DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT smemu6)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/roms/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/smemu6/roms
            COMPONENT smemu6)
    install(FILES ${CMAKE_SOURCE_DIR}/smemu6.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            COMPONENT smemu6)
    install(FILES ${CMAKE_SOURCE_DIR}/web/smaky6-256.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME smaky6.png
            COMPONENT smemu6)
endif()

# ── CPack / .deb packaging ────────────────────────────────────────────────────
if(NOT EMSCRIPTEN)
    set(CPACK_PACKAGE_NAME            "smemu6")
    set(CPACK_PACKAGE_VENDOR          "Marcel Prisi")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Emulator for the 1978 Swiss Smaky 6 Z80 computer")
    # Derive version from git describe (e.g. v1.2-3-gabcdef → 1.2-3-gabcdef).
    # Debian version fields must not contain hyphens in the upstream part, so
    # we replace the first hyphen (between tag and commit count) with a tilde
    # which sorts correctly as a pre-release in dpkg version comparison.
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        # Strip leading 'v'
        string(REGEX REPLACE "^v" "" GIT_VERSION "${GIT_VERSION}")
        # Replace first hyphen (tag-count separator) with ~ for deb compat
        string(REGEX REPLACE "^([^-]+)-([0-9]+)-" "\\1~\\2~" GIT_VERSION "${GIT_VERSION}")
    else()
        set(GIT_VERSION "0.0.0")
    endif()
    set(CPACK_PACKAGE_VERSION "${GIT_VERSION}")
    set(CPACK_PACKAGE_CONTACT         "Marcel Prisi")
    set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_STRIP_FILES             TRUE)

    # Debian-specific
    set(CPACK_GENERATOR               "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION  "games")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS  "libsdl2-2.0-0 (>= 2.0.0)")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mprisi/smemu6")
    set(CPACK_DEBIAN_FILE_NAME        DEB-DEFAULT)  # smemu6_0.1.0_amd64.deb
    set(CPACK_COMPONENTS_ALL          smemu6)
    set(CPACK_DEB_COMPONENT_INSTALL   ON)
    set(CPACK_DEBIAN_SMEMU6_PACKAGE_NAME "smemu6")  # suppress component suffix in filename

    include(CPack)
endif()
