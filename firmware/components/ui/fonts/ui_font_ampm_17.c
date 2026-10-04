/*******************************************************************************
 * Size: 17 px
 * Bpp: 4
 * Opts: --bpp 4 --size 17 --format lvgl --lv-include lvgl.h --font fonts/tinybar/TinyBarCondensed-Bold.ttf --autohint-off --symbols AMP --lv-font-name ui_font_ampm_17 -o components/ui/fonts/ui_font_ampm_17.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef UI_FONT_AMPM_17
#define UI_FONT_AMPM_17 1
#endif

#if UI_FONT_AMPM_17

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0041 "A" */
    0x0, 0x2f, 0xfa, 0x0, 0x36, 0x80, 0xc, 0x3,
    0x20, 0x1, 0x0, 0x31, 0x80, 0x81, 0x0, 0xc,
    0x9, 0x41, 0x0, 0xa, 0xe, 0x61, 0xe0, 0xc,
    0xd, 0xe0, 0x40, 0x3, 0x81, 0x28, 0x10, 0x9,
    0x5, 0xe8, 0x0, 0xd4, 0x0, 0x22, 0x0, 0x21,
    0x81, 0x7f, 0x94, 0x37, 0x1, 0xc0, 0x1a, 0x8,

    /* U+004D "M" */
    0x4f, 0xf2, 0x0, 0xf7, 0xc8, 0x5, 0x21, 0x22,
    0x6, 0x1, 0x13, 0xb8, 0x3, 0xe8, 0x0, 0xf9,
    0x0, 0x48, 0x3, 0xa0, 0x2c, 0x40, 0x38, 0xad,
    0x40, 0x3e, 0x40, 0xf, 0xfe, 0xd9, 0x80,

    /* U+0050 "P" */
    0x4f, 0xfd, 0x84, 0x1, 0xe3, 0xc1, 0x0, 0xb2,
    0xc0, 0x5c, 0x2, 0x14, 0x20, 0xe0, 0xc, 0x64,
    0x1c, 0x1, 0x6e, 0x0, 0x1c, 0x3, 0x8a, 0xc4,
    0x2, 0xcf, 0xd4, 0x0, 0xc2, 0x1, 0xff, 0xcb,
    0x30, 0xc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 131, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 149, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 79, .adv_w = 126, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0xc, 0xf
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 65, .range_length = 16, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    1, 1,
    3, 1
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    4, -9
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 2,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_ampm_17 = {
#else
lv_font_t ui_font_ampm_17 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_AMPM_17*/

