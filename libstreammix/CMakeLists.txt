cmake_minimum_required(VERSION 3.27)

# ─────────────────────────────────────────────────────────────────
#  libstreammix — the headless core engine
# ─────────────────────────────────────────────────────────────────

set(LIBSTREAMMIX_SOURCES
    src/core/context.cpp
    src/core/data_source.cpp
    src/capture/source.cpp
    src/capture/ffmpeg_source.cpp
    src/capture/zoom_source.cpp
    src/capture/omt_source.cpp
    src/capture/ndi_source.cpp
    src/capture/title_source.cpp
    src/compositor/compositor.cpp
    src/audio/audio_mixer.cpp
    src/audio/audio_dsp.cpp
    src/output/rtmp_output.cpp
    src/output/web_server.cpp
    src/output/virtual_cam.cpp
    src/output/ndi_output.cpp
    src/scene/scene.cpp
    src/scene/scene_manager.cpp
    src/control/control_surface.cpp
    src/control/ptz_controller.cpp
    src/replay/instant_replay.cpp
    src/plugin/plugin_registry.cpp
    src/core/preset_manager.cpp
    src/compositor/chroma_key.cpp
    src/compositor/virtual_set.cpp
    src/compositor/shader_pipeline.cpp
    src/output/stream_manager.cpp
    src/output/multicorder.cpp
    src/output/record_output.cpp
)

add_library(streammix SHARED ${LIBSTREAMMIX_SOURCES})

target_include_directories(streammix
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(streammix
    PUBLIC
        PkgConfig::FFMPEG
        OpenGL::GL
    PRIVATE
        Qt6::Core
)

if(WIN32)
    target_link_libraries(streammix PRIVATE ws2_32 winmm shlwapi)
endif()

target_compile_definitions(streammix PRIVATE STREAMMIX_BUILDING_LIB)

set_target_properties(streammix PROPERTIES
    OUTPUT_NAME "streammix"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   0
)

install(TARGETS streammix
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/streammix
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
