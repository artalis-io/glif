#define NK_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_IO
/* stb_rect_pack and stb_truetype are compiled in font.c — avoid duplicate symbols */
#define NK_NO_STB_RECT_PACK_IMPLEMENTATION
#define NK_NO_STB_TRUETYPE_IMPLEMENTATION
#include "nuklear.h"
