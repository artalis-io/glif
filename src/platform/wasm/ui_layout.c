#include "ui_layout.h"
#include <string.h>

#define TOOLBAR_H 48
#define MOBILE_BREAKPOINT 600
#define CONTROLS_H 120

/* Forward-declared element IDs */
static const Clay_Color bg_dark   = { 26, 26, 26, 255 };
static const Clay_Color bg_toolbar = { 34, 34, 34, 255 };
static const Clay_Color text_color = { 204, 204, 204, 255 };

Clay_Dimensions ui_measure_text(Clay_StringSlice text,
                                Clay_TextElementConfig *config,
                                void *userData) {
    (void)userData;
    /* Approximate monospace measurement: each char is ~0.6 * fontSize wide */
    float w = (float)text.length * (float)config->fontSize * 0.6f;
    float h = (float)config->fontSize;
    return (Clay_Dimensions){ w, h };
}

void ui_layout_build(UiLayout *layout, int canvas_w, int canvas_h) {
    memset(layout, 0, sizeof(*layout));
    layout->is_mobile = canvas_w < MOBILE_BREAKPOINT;

    if (!layout->is_mobile) {
        /* Desktop: toolbar on top, viewport fills remaining space */
        CLAY(CLAY_ID("Root"), {
            .layout = {
                .sizing = {
                    .width = CLAY_SIZING_FIXED((float)canvas_w),
                    .height = CLAY_SIZING_FIXED((float)canvas_h)
                },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = bg_dark,
        }) {
            /* Toolbar */
            CLAY(CLAY_ID("Toolbar"), {
                .layout = {
                    .sizing = {
                        .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIXED(TOOLBAR_H)
                    },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childGap = 12,
                    .padding = { 16, 16, 8, 8 },
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = bg_toolbar,
            }) {
                CLAY_TEXT(CLAY_STRING("Edge"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Contrast"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Adaptive"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Resolution"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Webcam"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
            }

            /* Viewport */
            CLAY(CLAY_ID("Viewport"), {
                .layout = {
                    .sizing = {
                        .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_GROW(0),
                    },
                },
                .backgroundColor = bg_dark,
            }) {}
        }
    } else {
        /* Mobile: viewport on top, controls at bottom */
        CLAY(CLAY_ID("Root"), {
            .layout = {
                .sizing = {
                    .width = CLAY_SIZING_FIXED((float)canvas_w),
                    .height = CLAY_SIZING_FIXED((float)canvas_h),
                },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = bg_dark,
        }) {
            /* Viewport */
            CLAY(CLAY_ID("Viewport"), {
                .layout = {
                    .sizing = {
                        .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_GROW(0),
                    },
                },
                .backgroundColor = bg_dark,
            }) {}

            /* Controls at bottom */
            CLAY(CLAY_ID("Controls"), {
                .layout = {
                    .sizing = {
                        .width = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIXED(CONTROLS_H),
                    },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = 8,
                    .padding = { 12, 12, 8, 8 },
                },
                .backgroundColor = bg_toolbar,
            }) {
                CLAY_TEXT(CLAY_STRING("Edge / Contrast"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Adaptive / Res"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
                CLAY_TEXT(CLAY_STRING("Camera"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = text_color,
                }));
            }
        }
    }

    /* NOTE: bounding boxes are computed by Clay_EndLayout(), called separately.
       Call ui_layout_get_bounds() after Clay_EndLayout() to retrieve them. */
}

void ui_layout_get_bounds(UiLayout *layout) {
    Clay_ElementData vp = Clay_GetElementData(CLAY_ID("Viewport"));
    if (vp.found) layout->viewport = vp.boundingBox;

    Clay_ElementData tb = Clay_GetElementData(
        layout->is_mobile ? CLAY_ID("Controls") : CLAY_ID("Toolbar"));
    if (tb.found) layout->toolbar = tb.boundingBox;
}
