/* png_write.h: a tiny PNG writer (8-bit RGB, stored deflate blocks, no dependencies). Owner: ui builder. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);
