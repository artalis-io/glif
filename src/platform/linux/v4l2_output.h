#ifndef V4L2_OUTPUT_H
#define V4L2_OUTPUT_H

#ifdef __linux__

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    int width, height;
    size_t frame_size;
} V4l2Output;

int  v4l2_output_init(V4l2Output *vo, const char *device, int width, int height);
void v4l2_output_frame(V4l2Output *vo, const uint8_t *rgb24_pixels);
void v4l2_output_free(V4l2Output *vo);

#endif /* __linux__ */
#endif
