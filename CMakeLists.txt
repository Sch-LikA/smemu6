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

# ── Version string (from git describe; available to all build targets) ────────
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SMEMU6_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
else()
    set(SMEMU6_VERSION "dev")
endif()
message(STATUS "smemu6 version: ${SMEMU6_VERSION}")

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
    src/virtual_floppy.c
    src/sound.c
    src/debug.c
    src/rtc.c
    src/winchester.c
)

if(NOT EMSCRIPTEN)
    add_library(smaky6_st_symbols STATIC
        sdcc/smaky6_st_symbols.c
    )
    target_include_directories(smaky6_st_symbols PUBLIC sdcc)

    add_executable(dump_smaky6_st_symbols
        sdcc/dump_smaky6_st_symbols.c
    )
    target_link_libraries(dump_smaky6_st_symbols PRIVATE smaky6_st_symbols)

    add_executable(export_smaky6_st_symbols
        sdcc/export_smaky6_st_symbols.c
    )
    target_link_libraries(export_smaky6_st_symbols PRIVATE smaky6_st_symbols)

    if(MSVC)
        target_compile_options(dump_smaky6_st_symbols PRIVATE /W3)
        target_compile_options(export_smaky6_st_symbols PRIVATE /W3)
        target_compile_options(smaky6_st_symbols PRIVATE /W3)
    else()
        target_compile_options(dump_smaky6_st_symbols PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(export_smaky6_st_symbols PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(smaky6_st_symbols PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set(SMAKY6_ST_BOOTDIR ${CMAKE_SOURCE_DIR}/private/floppies/extracted/Sys2-2-boot)
    set(SMAKY6_SM6_ST ${SMAKY6_ST_BOOTDIR}/SM6.ST)
    set(SMAKY6_FLO_ST ${SMAKY6_ST_BOOTDIR}/FLO.ST)

    if(EXISTS ${SMAKY6_SM6_ST} AND EXISTS ${SMAKY6_FLO_ST})
        set(SMAKY6_ST_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated-st)
        file(MAKE_DIRECTORY ${SMAKY6_ST_GENERATED_DIR})

        set(SMAKY6_SM6_HEADER ${SMAKY6_ST_GENERATED_DIR}/generated_sm6_st_symbols.h)
        set(SMAKY6_SM6_JSON   ${SMAKY6_ST_GENERATED_DIR}/generated_sm6_st_symbols.json)
        set(SMAKY6_FLO_HEADER ${SMAKY6_ST_GENERATED_DIR}/generated_flo_st_symbols.h)
        set(SMAKY6_FLO_JSON   ${SMAKY6_ST_GENERATED_DIR}/generated_flo_st_symbols.json)

        add_custom_command(
            OUTPUT ${SMAKY6_SM6_HEADER} ${SMAKY6_SM6_JSON}
            COMMAND export_smaky6_st_symbols --header ${SMAKY6_SM6_ST} ${SMAKY6_SM6_HEADER}
            COMMAND export_smaky6_st_symbols --json ${SMAKY6_SM6_ST} ${SMAKY6_SM6_JSON}
            DEPENDS export_smaky6_st_symbols ${SMAKY6_SM6_ST}
            COMMENT "Generating SM6.ST exports"
        )

        add_custom_command(
            OUTPUT ${SMAKY6_FLO_HEADER} ${SMAKY6_FLO_JSON}
            COMMAND export_smaky6_st_symbols --header ${SMAKY6_FLO_ST} ${SMAKY6_FLO_HEADER}
            COMMAND export_smaky6_st_symbols --json ${SMAKY6_FLO_ST} ${SMAKY6_FLO_JSON}
            DEPENDS export_smaky6_st_symbols ${SMAKY6_FLO_ST}
            COMMENT "Generating FLO.ST exports"
        )

        add_custom_target(smaky6_st_exports
            DEPENDS ${SMAKY6_SM6_HEADER} ${SMAKY6_SM6_JSON} ${SMAKY6_FLO_HEADER} ${SMAKY6_FLO_JSON}
        )

        add_executable(smaky6_st_generated_example
            sdcc/smaky6_st_generated_example.c
            ${SMAKY6_SM6_HEADER}
        )
        add_dependencies(smaky6_st_generated_example smaky6_st_exports)
        target_include_directories(smaky6_st_generated_example PRIVATE ${SMAKY6_ST_GENERATED_DIR})
        if(MSVC)
            target_compile_options(smaky6_st_generated_example PRIVATE /W3)
        else()
            target_compile_options(smaky6_st_generated_example PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

# ── Platform icon resources ────────────────────────────────────────────────────
# Windows: embed .ico via .rc resource file so the .exe has a taskbar/Explorer icon
if(WIN32)
    target_sources(smemu6 PRIVATE "${CMAKE_SOURCE_DIR}/gfx/smaky6.rc")
endif()

# macOS: build as .app bundle with .icns icon
if(APPLE)
    set_target_properties(smemu6 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Smaky 6 Emulator"
        MACOSX_BUNDLE_GUI_IDENTIFIER "ch.mprisi.smemu6"
        MACOSX_BUNDLE_ICON_FILE "smaky6.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
    set_source_files_properties("${CMAKE_SOURCE_DIR}/gfx/smaky6.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(smemu6 PRIVATE "${CMAKE_SOURCE_DIR}/gfx/smaky6.icns")
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
    set(EM_LINK_FLAGS "-s USE_SDL=2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=67108864 -s EXPORTED_RUNTIME_METHODS=[ccall,cwrap,FS,callMain] -s EXPORTED_FUNCTIONS=[_main,_smemu6_reset,_smemu6_mount_dx0,_smemu6_mount_dx1,_smemu6_get_version] -s MODULARIZE=0 -s FORCE_FILESYSTEM=1")
    # Only preload the Phantom boot ROM; chargen is built into the emulator.
    foreach(_rom samos_sys17.rom)
        if(EXISTS "${CMAKE_SOURCE_DIR}/roms/${_rom}")
            string(APPEND EM_LINK_FLAGS " --preload-file \"${CMAKE_SOURCE_DIR}/roms/${_rom}@/roms/${_rom}\"")
        endif()
    endforeach()
    # Preload the bundled floppy library so the launcher can offer built-in
    # disk selections before the emulator starts.
    if(EXISTS "${CMAKE_SOURCE_DIR}/floppies")
        string(APPEND EM_LINK_FLAGS " --preload-file \"${CMAKE_SOURCE_DIR}/floppies@/floppies\"")
    endif()
    set_target_properties(smemu6 PROPERTIES LINK_FLAGS "${EM_LINK_FLAGS}")

    # Keep the web shell assets in sync even when only HTML/CSS changes and
    # the smemu6.js target itself does not need to relink.
    set(WEB_ASSET_SOURCES
        "${CMAKE_SOURCE_DIR}/web/index.html"
        "${CMAKE_SOURCE_DIR}/web/favicon.ico"
        "${CMAKE_SOURCE_DIR}/web/smaky6-128.png"
        "${CMAKE_SOURCE_DIR}/web/smaky6-256.png"
        "${CMAKE_SOURCE_DIR}/web/smaky6-logo-header.png"
    )
    set(WEB_ASSET_OUTPUTS
        "${CMAKE_BINARY_DIR}/index.html"
        "${CMAKE_BINARY_DIR}/favicon.ico"
        "${CMAKE_BINARY_DIR}/smaky6-128.png"
        "${CMAKE_BINARY_DIR}/smaky6-256.png"
        "${CMAKE_BINARY_DIR}/smaky6-logo-header.png"
    )
    add_custom_command(
        OUTPUT ${WEB_ASSET_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/index.html"
            "${CMAKE_BINARY_DIR}/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/favicon.ico"
            "${CMAKE_BINARY_DIR}/favicon.ico"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/smaky6-128.png"
            "${CMAKE_BINARY_DIR}/smaky6-128.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/smaky6-256.png"
            "${CMAKE_BINARY_DIR}/smaky6-256.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/smaky6-logo-header.png"
            "${CMAKE_BINARY_DIR}/smaky6-logo-header.png"
        DEPENDS ${WEB_ASSET_SOURCES}
        COMMENT "Copying web shell assets to build directory"
    )
    add_custom_target(smemu6_web_assets DEPENDS ${WEB_ASSET_OUTPUTS})
    add_dependencies(smemu6 smemu6_web_assets)

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

# ── Version compile definition ───────────────────────────────────────────────
target_compile_definitions(smemu6 PRIVATE SMEMU6_VERSION="${SMEMU6_VERSION}")

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(smemu6 PRIVATE /W3)
else()
    target_compile_options(smemu6 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT EMSCRIPTEN)
    enable_testing()

    add_executable(smemu6_virtual_floppy_test
        src/virtual_floppy_test.c
        src/virtual_floppy.c
    )
    target_include_directories(smemu6_virtual_floppy_test PRIVATE src)
    if(MSVC)
        target_compile_options(smemu6_virtual_floppy_test PRIVATE /W3)
    else()
        target_compile_options(smemu6_virtual_floppy_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME smemu6_virtual_floppy_builder
             COMMAND smemu6_virtual_floppy_test)

    if(UNIX)
        add_test(
            NAME smemu6_virtual_floppy_dx1_list
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx1_list.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_virtual_floppy_dx1_type
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx1_type.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_virtual_floppy_dx1_subdir
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx1_subdir.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_virtual_floppy_dx1_subdir_list
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx1_subdir_list.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_virtual_floppy_dx1_cdir_overlay
            COMMAND /usr/bin/env bash
                ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx1_cdir_overlay.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_virtual_floppy_dx0_hostdir_boot
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_virtual_floppy_dx0_hostdir_boot.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME smemu6_winchester_overlay_delete
            COMMAND /usr/bin/env bash
                    ${CMAKE_SOURCE_DIR}/tools/check_winchester_overlay_delete.sh
                    ${CMAKE_BINARY_DIR}/smemu6
                    ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()

# ── ROM directory ─────────────────────────────────────────────────────────────
# Copy ROMs to build directory so the emulator can find them at runtime
if(EXISTS ${CMAKE_SOURCE_DIR}/roms)
    file(COPY ${CMAKE_SOURCE_DIR}/roms DESTINATION ${CMAKE_BINARY_DIR})
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/floppies)
    file(COPY ${CMAKE_SOURCE_DIR}/floppies DESTINATION ${CMAKE_BINARY_DIR})
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
if(NOT EMSCRIPTEN)
    include(GNUInstallDirs)
    if(APPLE)
        # Bundle install: ROMs go inside Contents/Resources/roms/ so that
        # SDL_GetBasePath() + chdir() in main.c can find them via "roms/".
        install(TARGETS smemu6 BUNDLE DESTINATION .
                COMPONENT smemu6)
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/roms/
                DESTINATION smemu6.app/Contents/Resources/roms
                COMPONENT smemu6)
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/floppies/
                DESTINATION smemu6.app/Contents/Resources/floppies
                COMPONENT smemu6
                FILES_MATCHING PATTERN "*.dsk" PATTERN "*.DSK")
    else()
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
    # Derive a deb-compatible version from SMEMU6_VERSION (strip 'v', replace
    # first hyphen with ~ so dpkg sorts it correctly as a pre-release).
    set(GIT_VERSION "${SMEMU6_VERSION}")
    string(REGEX REPLACE "^v" "" GIT_VERSION "${GIT_VERSION}")
    string(REGEX REPLACE "^([^-]+)-([0-9]+)-" "\\1~\\2~" GIT_VERSION "${GIT_VERSION}")
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
