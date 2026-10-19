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
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

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
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    list(APPEND CMAKE_PREFIX_PATH
        "/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-qt6-2026-07-15-universal"
        "/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-2026-07-15-universal"
        "${HOMEBREW_PREFIX}/opt/qt"
        "${HOMEBREW_PREFIX}/opt/ffmpeg@7"
        "${HOMEBREW_PREFIX}/opt/libsrt"
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
)
qt_standard_project_setup()

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
        libavdevice
    )
    pkg_check_modules(SRT IMPORTED_TARGET srt)
endif()

# Fallback: if pkg-config did not find FFmpeg, link from local universal deps
if(NOT TARGET PkgConfig::FFMPEG)
    set(OBS_DEPS_DIR "/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-2026-07-15-universal")
    if(EXISTS "${OBS_DEPS_DIR}/include/libavcodec/avcodec.h")
        add_library(PkgConfig::FFMPEG INTERFACE IMPORTED GLOBAL)
        target_include_directories(PkgConfig::FFMPEG INTERFACE "${OBS_DEPS_DIR}/include")
        target_link_libraries(PkgConfig::FFMPEG INTERFACE
            "${OBS_DEPS_DIR}/lib/libavdevice.dylib"
            "${OBS_DEPS_DIR}/lib/libavformat.dylib"
            "${OBS_DEPS_DIR}/lib/libavcodec.dylib"
            "${OBS_DEPS_DIR}/lib/libavfilter.dylib"
            "${OBS_DEPS_DIR}/lib/libswscale.dylib"
            "${OBS_DEPS_DIR}/lib/libswresample.dylib"
            "${OBS_DEPS_DIR}/lib/libavutil.dylib"
        )
        set(FFMPEG_FOUND TRUE)
        message(STATUS "Found FFmpeg in ${OBS_DEPS_DIR}")
    endif()
endif()

if(WIN32 AND NOT TARGET PkgConfig::FFMPEG)
    set(FFMPEG_WIN_SEARCH_PATHS
        "$ENV{FFMPEG_DIR}"
        "C:/ffmpeg"
        "C:/vcpkg/installed/x64-windows"
        "$ENV{VCPKG_ROOT}/installed/x64-windows"
    )
    foreach(SEARCH_PATH ${FFMPEG_WIN_SEARCH_PATHS})
        if(EXISTS "${SEARCH_PATH}/include/libavcodec/avcodec.h")
            add_library(PkgConfig::FFMPEG INTERFACE IMPORTED GLOBAL)
            target_include_directories(PkgConfig::FFMPEG INTERFACE "${SEARCH_PATH}/include")
            target_link_directories(PkgConfig::FFMPEG INTERFACE "${SEARCH_PATH}/lib")
            target_link_libraries(PkgConfig::FFMPEG INTERFACE
                avcodec avformat avutil swscale swresample avdevice
            )
            set(FFMPEG_FOUND TRUE)
            message(STATUS "Found Windows FFmpeg in ${SEARCH_PATH}")
            break()
        endif()
    endforeach()
endif()

find_package(OpenGL REQUIRED)

# ─── Compiler flags ─────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(APPLE)
            add_compile_options(-O3)
        else()
            add_compile_options(-O3 -march=native)
        endif()
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
