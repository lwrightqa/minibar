# TinyBar host tests: the pure-logic parts of core, calendar, net (proto/) and ui (view/), built with the host's gcc
# and run with ctest. Owner: lead developer (this file and tb_test.*); each builder owns test/host/<module>/.
#
#   cmake -S firmware/test/host -B firmware/build-host-<name>
#   cmake --build firmware/build-host-<name> -j && ctest --test-dir firmware/build-host-<name> --output-on-failure
#
# net's protocol code uses ESP-IDF's copy of cJSON (IDF_PATH, default /home/user/esp/esp-idf); nothing else from IDF.
cmake_minimum_required(VERSION 3.16)
project(tinybar_host_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
# Catch memory errors in the parsers and state machines.
add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
add_link_options(-fsanitize=address,undefined)

if(NOT DEFINED IDF_PATH)
    if(DEFINED ENV{IDF_PATH})
        set(IDF_PATH $ENV{IDF_PATH})
    else()
        set(IDF_PATH /home/user/esp/esp-idf)
    endif()
endif()

set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
add_subdirectory(${FW}/components/core core)
add_subdirectory(${FW}/components/calendar calendar)
add_subdirectory(${FW}/components/net net)
add_subdirectory(${FW}/components/ui ui)

enable_testing()

# One runner per module: every .c in test/host/<module>/ plus the shared main.
function(tb_test_module module)
    file(GLOB srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/${module}/*.c)
    add_executable(test_${module} ${CMAKE_CURRENT_LIST_DIR}/tb_test_main.c ${srcs})
    target_include_directories(test_${module} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/${module})
    target_compile_options(test_${module} PRIVATE -Wall -Wextra)
    target_link_libraries(test_${module} PRIVATE ${ARGN})
    add_test(NAME ${module} COMMAND test_${module})
    # Fixtures (sample .ics files and the like) are read from here.
    set_tests_properties(${module} PROPERTIES ENVIRONMENT "TB_FIXTURES=${CMAKE_CURRENT_LIST_DIR}/${module}/fixtures")
endfunction()

tb_test_module(core tb_core)
tb_test_module(calendar tb_calendar tb_core)
tb_test_module(net tb_net_proto tb_calendar tb_core)
tb_test_module(ui tb_ui_view tb_core)
