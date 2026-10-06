cmake_minimum_required(VERSION 3.16)
project(window_to_steamvr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

# OPENVR_ROOT can point to an OpenVR SDK checkout (which contains headers/ and lib/linux64/).
find_path(OPENVR_INCLUDE_DIR openvr.h
    HINTS "${OPENVR_ROOT}/headers" "$ENV{OPENVR_ROOT}/headers"
    PATH_SUFFIXES openvr)
find_library(OPENVR_LIBRARY NAMES openvr_api
    HINTS "${OPENVR_ROOT}/lib/linux64" "$ENV{OPENVR_ROOT}/lib/linux64")
if(NOT OPENVR_INCLUDE_DIR OR NOT OPENVR_LIBRARY)
    message(FATAL_ERROR "OpenVR SDK not found. Install libopenvr-dev or set OPENVR_ROOT to the OpenVR SDK directory.")
endif()

add_executable(window-to-steamvr src/main.cpp)
target_include_directories(window-to-steamvr PRIVATE "${OPENVR_INCLUDE_DIR}")
target_link_libraries(window-to-steamvr PRIVATE
    PkgConfig::GLIB PkgConfig::GST PkgConfig::SDL OpenGL::GL "${OPENVR_LIBRARY}")
target_compile_options(window-to-steamvr PRIVATE -Wall -Wextra -Wpedantic)
