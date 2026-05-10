# cmake/Emscripten.cmake
#
# Emscripten toolchain file for smemu6.
# Usage (from project root):
#   cmake --preset web
# or:
#   emcmake cmake -S . -B build-web -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
#
# Requires the Emscripten SDK to be installed and on PATH.

# Delegate to Emscripten's own CMake toolchain/platform file.
# Try system install first, then fall back to EMSDK environment.
if(EXISTS "/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    include("/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake")
elseif(DEFINED ENV{EMSDK})
    include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
            OPTIONAL RESULT_VARIABLE _em_tc)
    if(NOT _em_tc)
        include("$ENV{EMSDK}/emscripten/cmake/Modules/Platform/Emscripten.cmake"
                OPTIONAL)
    endif()
else()
    message(FATAL_ERROR
        "Emscripten not found.  Install the emsdk and source emsdk_env.sh, "
        "or install via your system package manager (apt install emscripten).")
endif()
