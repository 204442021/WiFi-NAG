cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" PROJECT_VER LIMIT_COUNT 1)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
if(PROJECT_VER STREQUAL "")
    message(FATAL_ERROR "VERSION must contain the firmware version")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ev_open_can_tools)
