cmake_minimum_required(VERSION 3.27)

project(StreamMixApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Network
)

set(STREAMMIX_APP_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/audio_player.cpp
    src/widgets/monitor_widget.cpp
    src/widgets/transition_widget.cpp
    src/widgets/input_card_widget.cpp
    src/widgets/audio_mixer_widget.cpp
    src/widgets/ptz_panel.cpp
    src/widgets/replay_panel.cpp
    src/dialogs/settings_dialog.cpp
    src/dialogs/input_settings_dialog.cpp
)

set(STREAMMIX_APP_HEADERS
    src/main_window.h
    src/audio_player.h
    src/widgets/monitor_widget.h
    src/widgets/transition_widget.h
    src/widgets/input_card_widget.h
    src/widgets/audio_mixer_widget.h
    src/widgets/ptz_panel.h
    src/widgets/replay_panel.h
    src/dialogs/settings_dialog.h
    src/dialogs/input_settings_dialog.h
)

add_executable(streammix-app
    ${STREAMMIX_APP_SOURCES}
    ${STREAMMIX_APP_HEADERS}
)

target_include_directories(streammix-app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
        ${CMAKE_SOURCE_DIR}/libstreammix/include
)

target_link_libraries(streammix-app
    PRIVATE
        streammix
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Network
)

if(APPLE)
    target_link_libraries(streammix-app PRIVATE "-framework AudioToolbox")
endif()

set_target_properties(streammix-app PROPERTIES
    OUTPUT_NAME "StreamMix"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "StreamMix"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.streammix.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS streammix-app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
