cmake_minimum_required(VERSION 3.21)

project(rood VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROOD_BUILD_GUI "Build the Qt Widgets shell when Qt 6 is available" ON)
option(ROOD_REQUIRE_QT "Fail configuration when Qt 6 Widgets is unavailable" OFF)
option(ROOD_BUILD_PORTAUDIO_PROBE "Build the PortAudio device probe when PortAudio is available" ON)
option(ROOD_BUILD_MEDIA_DEPENDENCY_PROBE "Link and inspect the pinned media development libraries" OFF)

add_library(rood_engine STATIC
    src/engine/ChannelRouter.cpp
)
target_include_directories(rood_engine PUBLIC include)
target_compile_features(rood_engine PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(rood_engine PRIVATE /W4)
else()
    target_compile_options(rood_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rood_diag apps/diag/main.cpp)
target_link_libraries(rood_diag PRIVATE rood_engine)

if(ROOD_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6Widgets_FOUND)
        add_executable(rood_gui WIN32 apps/gui/main.cpp)
        target_link_libraries(rood_gui PRIVATE rood_engine Qt6::Widgets)
        if(MSVC)
            target_compile_options(rood_gui PRIVATE /utf-8)
        endif()
        message(STATUS "Qt Widgets GUI: enabled (${Qt6_VERSION})")
    else()
        if(ROOD_REQUIRE_QT)
            message(FATAL_ERROR "Qt 6 Widgets is required but was not found. Set Qt6_DIR or CMAKE_PREFIX_PATH.")
        endif()
        message(STATUS "Qt 6 Widgets not found; GUI target skipped. Set Qt6_DIR or CMAKE_PREFIX_PATH after installing Qt.")
    endif()
endif()

if(ROOD_BUILD_PORTAUDIO_PROBE)
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
    find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_x64)
    if(WIN32)
        find_path(PORTAUDIO_WASAPI_INCLUDE_DIR pa_win_wasapi.h HINTS "${PORTAUDIO_INCLUDE_DIR}")
    endif()
    if(PORTAUDIO_INCLUDE_DIR AND PORTAUDIO_LIBRARY AND
       (NOT WIN32 OR PORTAUDIO_WASAPI_INCLUDE_DIR))
        add_executable(rood_pa_probe apps/pa_probe/main.cpp)
        target_include_directories(rood_pa_probe PRIVATE "${PORTAUDIO_INCLUDE_DIR}")
        if(WIN32)
            target_include_directories(rood_pa_probe PRIVATE "${PORTAUDIO_WASAPI_INCLUDE_DIR}")
        endif()
        target_link_libraries(rood_pa_probe PRIVATE "${PORTAUDIO_LIBRARY}")
        message(STATUS "PortAudio device probe: enabled (${PORTAUDIO_LIBRARY})")
    else()
        message(STATUS "PortAudio development files not found; probe target skipped.")
    endif()
endif()

if(ROOD_BUILD_MEDIA_DEPENDENCY_PROBE)
    find_path(ROOD_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
    find_library(ROOD_AVCODEC_LIBRARY NAMES avcodec REQUIRED)
    find_library(ROOD_AVUTIL_LIBRARY NAMES avutil REQUIRED)
    find_path(ROOD_LIBSRT_INCLUDE_DIR srt/srt.h REQUIRED)
    find_library(ROOD_LIBSRT_LIBRARY NAMES srt REQUIRED)
    find_path(ROOD_SPOUT_INCLUDE_DIR SpoutLibrary/SpoutLibrary.h REQUIRED)
    find_library(ROOD_SPOUT_LIBRARY NAMES SpoutLibrary REQUIRED)
    find_path(ROOD_OMT_INCLUDE_DIR libomt.h HINTS "${ROOD_OMT_DIR}" REQUIRED)
    find_library(ROOD_OMT_LIBRARY NAMES libomt HINTS "${ROOD_OMT_DIR}" REQUIRED)

    add_executable(rood_deps_probe apps/deps_probe/main.cpp)
    target_include_directories(rood_deps_probe PRIVATE
        "${ROOD_FFMPEG_INCLUDE_DIR}"
        "${ROOD_LIBSRT_INCLUDE_DIR}"
        "${ROOD_SPOUT_INCLUDE_DIR}"
        "${ROOD_OMT_INCLUDE_DIR}")
    target_link_libraries(rood_deps_probe PRIVATE
        "${ROOD_AVCODEC_LIBRARY}"
        "${ROOD_AVUTIL_LIBRARY}"
        "${ROOD_LIBSRT_LIBRARY}"
        "${ROOD_SPOUT_LIBRARY}"
        "${ROOD_OMT_LIBRARY}")
    if(MSVC)
        target_compile_options(rood_deps_probe PRIVATE /utf-8)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(channel_router_test tests/ChannelRouterTest.cpp)
    target_link_libraries(channel_router_test PRIVATE rood_engine)
    add_test(NAME channel_router COMMAND channel_router_test)
endif()
