# MiniBar firmware: ESP-IDF v5.4 project for the Waveshare ESP32-S3-Touch-LCD-3.49 V2.
# See README.md (build and flash) and ARCHITECTURE.md (modules, tasks, events).
#
# Build in your own build directory, never the shared default one:
#   idf.py -B build-<name> -D SDKCONFIG=build-<name>/sdkconfig build
cmake_minimum_required(VERSION 3.16)

# The firmware version reported as "fw" in GET /api/v1/info, the USB hello reply and the mDNS TXT record.
set(PROJECT_VER "1.0.3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# cJSON allows 1,000 levels of nesting by default, at about 64 bytes of stack each, and its parser and cJSON_Delete
# both recurse: a hostile request could run the app task's 12 KB stack out. The deepest real request is 3 levels.
# cJSON.h only sets the limit #ifndef, so this reaches IDF's json component too (net's router checks it again with
# net_json_depth_ok, and asserts the value it sees). The host tests set the same (test/host/CMakeLists.txt).
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=16" APPEND)

project(minibar)
