#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "clay.h"

typedef struct {
    Clay_BoundingBox viewport;   /* ASCII art viewport bounds */
    Clay_BoundingBox toolbar;    /* Toolbar bounds */
    int is_mobile;               /* True if width < 600 */
} UiLayout;

/* Must be called after Clay_BeginLayout() and before Clay_EndLayout().
   Declares the Clay element tree for the app layout. */
void ui_layout_build(UiLayout *layout, int canvas_w, int canvas_h);

/* Clay text measurement callback — uses Nuklear font */
Clay_Dimensions ui_measure_text(Clay_StringSlice text,
                                Clay_TextElementConfig *config,
                                void *userData);

#endif /* UI_LAYOUT_H */
