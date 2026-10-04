# TinyBar firmware: ESP-IDF v5.4 project for the Waveshare ESP32-S3-Touch-LCD-3.49 V2.
# See README.md (build and flash) and ARCHITECTURE.md (modules, tasks, events).
#
# Build in your own build directory, never the shared default one:
#   idf.py -B build-<name> -D SDKCONFIG=build-<name>/sdkconfig build
cmake_minimum_required(VERSION 3.16)

# The firmware version reported as "fw" in GET /api/v1/info, the USB hello reply and the mDNS TXT record.
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinybar)
