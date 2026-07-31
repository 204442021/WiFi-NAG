cmake_minimum_required(VERSION 3.16)

# Keep the ESP-IDF application descriptor aligned with the firmware version
# shown by the WebUI and injected by scripts/platformio_sync_profile.py.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" PROJECT_VER LIMIT_COUNT 1)
string(STRIP "${PROJECT_VER}" PROJECT_VER)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ev_open_can_tools)
