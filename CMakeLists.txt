cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" PROJECT_VER LIMIT_COUNT 1)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
if(PROJECT_VER STREQUAL "")
    message(FATAL_ERROR "VERSION must contain the firmware version")
endif()

# PlatformIO's ESP-IDF file-api adapter misparses quoted PROJECT_VER values
# containing literal spaces. Encode spaces as C string escapes so the firmware
# still reports the display version verbatim while clean PlatformIO builds work.
string(REPLACE " " "\\\\\\\\x20" PROJECT_VER "${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ev_open_can_tools)
