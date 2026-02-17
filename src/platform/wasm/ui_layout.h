#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "clay.h"

typedef struct {
    Clay_BoundingBox viewport;   /* ASCII art viewport bounds */
    Clay_BoundingBox toolbar;    /* Toolbar bounds */
    int is_mobile;               /* True if width < 600 */
    int toolbar_wrap;            /* True if toolbar needs 2 rows (600-767) */
} UiLayout;

/* Must be called after Clay_BeginLayout() and before Clay_EndLayout().
   Declares the Clay element tree for the app layout. */
void ui_layout_build(UiLayout *layout, int canvas_w, int canvas_h);

/* Must be called AFTER Clay_EndLayout() to retrieve computed bounding boxes. */
void ui_layout_get_bounds(UiLayout *layout);

/* Clay text measurement callback — uses Nuklear font */
Clay_Dimensions ui_measure_text(Clay_StringSlice text,
                                Clay_TextElementConfig *config,
                                void *userData);

#endif /* UI_LAYOUT_H */
