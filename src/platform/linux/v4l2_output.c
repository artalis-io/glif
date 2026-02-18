#ifdef __linux__

#include "v4l2_output.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int v4l2_output_init(V4l2Output *vo, const char *device, int width, int height) {
    memset(vo, 0, sizeof(*vo));
    vo->fd = -1;

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open v4l2 device '%s'\n", device);
        return -1;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = (unsigned)width;
    fmt.fmt.pix.height = (unsigned)height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.sizeimage = (unsigned)(width * height * 3);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "error: VIDIOC_S_FMT failed on '%s'\n", device);
        close(fd);
        return -1;
    }

    vo->fd = fd;
    vo->width = width;
    vo->height = height;
    vo->frame_size = (size_t)width * (size_t)height * 3;

    fprintf(stderr, "v4l2: opened %s (%dx%d RGB24)\n", device, width, height);
    return 0;
}

void v4l2_output_frame(V4l2Output *vo, const uint8_t *rgb24_pixels) {
    if (vo->fd < 0) return;
    ssize_t n = write(vo->fd, rgb24_pixels, vo->frame_size);
    (void)n; /* best-effort write to v4l2 device */
}

void v4l2_output_free(V4l2Output *vo) {
    if (vo->fd >= 0) {
        close(vo->fd);
        vo->fd = -1;
    }
}

#endif /* __linux__ */
