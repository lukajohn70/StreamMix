cmake_minimum_required(VERSION 3.27)

project(StreamMix
    VERSION 1.0.0
    DESCRIPTION "Cross-platform live video production software"
    LANGUAGES CXX C
)

# ─── C++ Standard ───────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build type default ─────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ─── Output directories ─────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── macOS specifics ────────────────────────────────────────────
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")
    # Homebrew prefix (Intel: /usr/local, Apple Silicon: /opt/homebrew)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    list(APPEND CMAKE_PREFIX_PATH
        "${HOMEBREW_PREFIX}/opt/qt"
        "${HOMEBREW_PREFIX}/opt/ffmpeg@7"
        "${HOMEBREW_PREFIX}/opt/portaudio"
        "${HOMEBREW_PREFIX}/opt/libsrt"
    )
    set(ENV{PKG_CONFIG_PATH}
        "${HOMEBREW_PREFIX}/opt/ffmpeg@7/lib/pkgconfig:${HOMEBREW_PREFIX}/opt/portaudio/lib/pkgconfig:${HOMEBREW_PREFIX}/opt/libsrt/lib/pkgconfig"
    )
endif()

# ─── Find packages ──────────────────────────────────────────────
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Network
    WebSockets
    Multimedia
)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
    libavdevice
)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(SRT IMPORTED_TARGET srt)  # optional — no REQUIRED

find_package(OpenGL REQUIRED)

# ─── Compiler flags ─────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# ─── Subprojects ────────────────────────────────────────────────
add_subdirectory(libstreammix)
add_subdirectory(streammix-app)

# ─── CPack Packaging ───────────────────────────────────────────
set(CPACK_PACKAGE_NAME "StreamMix")
set(CPACK_PACKAGE_VENDOR "StreamMix Live")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Professional Live Video Production Software (vMix 29 Parity)")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "StreamMix")
    set(CPACK_SYSTEM_NAME "macOS")
elseif(WIN32)
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID "D98A7F41-1122-4321-B33C-5E29618B4789")
    set(CPACK_SYSTEM_NAME "win64")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "StreamMix Team <support@streammix.live>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_SYSTEM_NAME "Linux")
endif()

include(CPack)

# ─── Summary ────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "══ StreamMix ${PROJECT_VERSION} ══")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std    : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt6 version: ${Qt6_VERSION}")
message(STATUS "  FFmpeg     : ${FFMPEG_VERSION}")
message(STATUS "  SRT        : ${SRT_FOUND}")
message(STATUS "")
