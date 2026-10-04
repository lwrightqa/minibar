# ui: TinyBar's LVGL screens in Bold Signal, plus the pure view model (view/) and build-time assets.
# On the device: an ESP-IDF component. On Linux: view/ alone for the host tests (tb_ui_view), and with LVGL_DIR set,
# the LVGL layer too (tb_ui) for the snapshot tool in host/. Owner: ui builder.
set(TB_UI_VIEW_SRCS view/ui_view.c view/ui_theme.c)
set(TB_UI_LVGL_SRCS src/ui.c src/ui_fonts.c)
file(GLOB TB_UI_FONT_SRCS ${CMAKE_CURRENT_LIST_DIR}/fonts/*.c)
set(TB_REPO_ASSETS ${CMAKE_CURRENT_LIST_DIR}/../../../assets)

# The tomato images, generated at build time from the repo's sprites.
function(tb_ui_gen_tomatoes out python_exe)
    add_custom_command(OUTPUT ${out}
        COMMAND ${python_exe} ${CMAKE_CURRENT_LIST_DIR}/tools/gen_tomatoes.py
                ${TB_REPO_ASSETS}/tomato_ripe.png ${TB_REPO_ASSETS}/tomato_unripe.png ${out}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/gen_tomatoes.py ${TB_REPO_ASSETS}/tomato_ripe.png
                ${TB_REPO_ASSETS}/tomato_unripe.png
        VERBATIM)
endfunction()

if(ESP_PLATFORM)
    idf_component_register(SRCS ${TB_UI_VIEW_SRCS} ${TB_UI_LVGL_SRCS} ${TB_UI_FONT_SRCS}
                           INCLUDE_DIRS "include"
                           PRIV_INCLUDE_DIRS "src"
                           REQUIRES core lvgl__lvgl)
    set(gen ${CMAKE_CURRENT_BINARY_DIR}/ui_tomatoes.c)
    tb_ui_gen_tomatoes(${gen} ${python})
    set_source_files_properties(${gen} PROPERTIES GENERATED TRUE)
    target_sources(${COMPONENT_LIB} PRIVATE ${gen})
else()
    add_library(tb_ui_view STATIC ${TB_UI_VIEW_SRCS})
    target_include_directories(tb_ui_view PUBLIC include)
    target_link_libraries(tb_ui_view PUBLIC tb_core)
    target_compile_options(tb_ui_view PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
    if(TARGET lvgl)
        # host/CMakeLists.txt builds LVGL first, then adds this directory.
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(gen ${CMAKE_CURRENT_BINARY_DIR}/ui_tomatoes.c)
        tb_ui_gen_tomatoes(${gen} ${Python3_EXECUTABLE})
        add_library(tb_ui STATIC ${TB_UI_LVGL_SRCS} ${TB_UI_FONT_SRCS} ${gen})
        target_include_directories(tb_ui PUBLIC include PRIVATE src)
        target_link_libraries(tb_ui PUBLIC tb_ui_view lvgl)
    endif()
endif()
