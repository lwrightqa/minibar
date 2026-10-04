# Host snapshot tool: renders TinyBar's screens with LVGL's software renderer on Linux and writes one PNG per scene,
# so they can be compared with docs/mockup.html without the board (tools/ref_scenes.js and tools/compare.py), and
# the touch test. Owner: ui builder.
#
#   cmake -S firmware/components/ui/host -B firmware/build-host-ui && cmake --build firmware/build-host-ui
#   firmware/build-host-ui/tinybar_snapshot <out-dir> [scene ...]      (--list shows the scenes)
#   ctest --test-dir firmware/build-host-ui                             (tinybar_touch_test)
#
# LVGL comes from the ESP-IDF component manager's copy (firmware/managed_components/lvgl__lvgl, present after any
# idf.py build or reconfigure), so the host renders with exactly the device's LVGL version.
cmake_minimum_required(VERSION 3.16)
project(tinybar_ui_host C CXX)
set(CMAKE_C_STANDARD 11)

set(FW ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(LVGL_DIR ${FW}/managed_components/lvgl__lvgl CACHE PATH "LVGL source")
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}. Run an idf.py build or reconfigure in firmware/ first.")
endif()

set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${LVGL_DIR} lvgl)

add_subdirectory(${FW}/components/core core)
add_subdirectory(${FW}/components/ui ui)

add_executable(tinybar_snapshot snapshot.c png_write.c)
target_link_libraries(tinybar_snapshot PRIVATE tb_ui tb_ui_view tb_core lvgl m)
target_compile_options(tinybar_snapshot PRIVATE -Wall -Wextra)

# Touch forwarding through LVGL's real input path (a virtual pointer), straight and flipped.
add_executable(tinybar_touch_test touch_test.c)
target_link_libraries(tinybar_touch_test PRIVATE tb_ui tb_ui_view tb_core lvgl m)
target_compile_options(tinybar_touch_test PRIVATE -Wall -Wextra)
enable_testing()
add_test(NAME ui_touch COMMAND tinybar_touch_test)
