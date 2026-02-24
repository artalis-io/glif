#ifndef GLIF_OUTPUT_H
#define GLIF_OUTPUT_H

#include "grid.h"
#include "font.h"
#include <stddef.h>
#include <stdio.h>

/* Print plain ASCII to stdout. */
void glif_output_plain(const GlifGrid *grid);

/* Print ANSI truecolor ASCII to stdout. */
void glif_output_ansi(const GlifGrid *grid);

/* Diff-based ANSI renderer for video mode — only emits changed cells.
 * dark_mode: 0 = color background + white glyphs
 *            1 = black background + colored glyphs */
typedef struct {
    char *buf;       /* write buffer (reused across frames) */
    size_t bufsize;
    uint8_t *prev;   /* previous frame: 4 bytes per cell [ch, r, g, b] */
    int cells;       /* total cell count */
    int cols;        /* grid columns (for cursor positioning) */
} GlifFrameDiff;

int glif_frame_diff_init(GlifFrameDiff *fd, int rows, int cols);
void glif_frame_diff_render(GlifFrameDiff *fd, const GlifGrid *grid, int dark_mode);
void glif_frame_diff_free(GlifFrameDiff *fd);

/* Write PPM image file with glyph rendering.
 * scale > 1 uses high-res render bitmaps for sharp text.
 * dark_mode: 0 = color background + white glyphs (default)
 *            1 = black background + colored glyphs */
int glif_output_ppm(const GlifGrid *grid, const GlifCharDatabase *db,
               const char *path, int scale, int dark_mode);

/* Write SVG image file using text elements with embedded @font-face.
 * dark_mode: 0 = colored rect background + white text
 *            1 = black background + colored text */
int glif_output_svg(const GlifGrid *grid, const GlifCharDatabase *db,
                    const char *path, int dark_mode);

/* Streaming PPM renderer for video mode — writes PPM frames to stdout.
 * Reuses allocated buffers across frames. */
typedef struct {
    uint8_t *pixels;      /* output pixel buffer */
    uint8_t **render_bmps; /* scaled glyph bitmaps (NULL if scale==1) */
    size_t img_w, img_h;  /* output image dimensions */
    int rw, rh;           /* render cell dimensions */
    int scale;
    int dark_mode;
} GlifPpmPipe;

int glif_ppm_pipe_init(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db,
                  int scale, int dark_mode);
void glif_ppm_pipe_render(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db);
void glif_ppm_pipe_frame(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db);
void glif_raw_pipe_frame(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db);
void glif_ppm_pipe_free(GlifPpmPipe *pp);

/* .glif binary format — see docs/roadmap.md for full spec. */
#define GLIF_MAGIC "GLIF"
#define GLIF_VERSION         1
#define GLIF_HEADER_SIZE 24
#define GLIF_FLAG_DARK       0x01
#define GLIF_FLAG_COMPRESSED 0x02
#define GLIF_FLAG_AUDIO      0x04

typedef struct BlipEncoder BlipEncoder;

typedef struct {
    FILE *file;
    uint8_t flags;    /* header flags (for audio bit patching) */
    uint32_t frames;
    int cells;        /* cols × rows */
    int cols, rows;   /* grid dimensions (for filtered codecs) */
    int err;          /* sticky write-error flag */
    /* compression buffers (all NULL when uncompressed) */
    int compressed;
    uint8_t *prev;      /* previous decoded frame, cells*4 */
    uint8_t *cur;       /* flattened current frame, cells*4 */
    uint8_t *work;      /* XOR scratch buffer, cells*4 */
    uint8_t *enc_buf;   /* encoding output, worst-case sized */
    size_t enc_cap;
    uint8_t *enc_buf2;  /* second encoding buffer for trying v3 codecs */
    size_t enc_cap2;
    int keyframe_interval; /* 0 = auto, >0 = force keyframe every N frames */
    int quant_shift;   /* bits to drop from RGB (0=off, 1-4) */
    int threshold;     /* max per-channel diff to snap (0=off) */
    /* audio (BLIP section — crushed PCM) */
    BlipEncoder *blip;            /* NULL when audio disabled */
    /* original audio (ORIG section) */
    uint8_t *orig_audio;          /* original audio bitstream, NULL if not kept */
    size_t orig_audio_len;
    uint32_t orig_codec;          /* e.g. 'OPUS' */
} GlifWriter;

int  glif_writer_init(GlifWriter *gw, const char *path,
                      int cols, int rows, int cell_w, int cell_h,
                      float fps, int dark_mode);
int  glif_writer_init_v2(GlifWriter *gw, const char *path,
                         int cols, int rows, int cell_w, int cell_h,
                         float fps, int dark_mode, int compressed);
void glif_writer_set_keyframe_interval(GlifWriter *gw, int interval);
void glif_writer_set_quant(GlifWriter *gw, int bits);
void glif_writer_set_threshold(GlifWriter *gw, int thresh);
void glif_writer_frame(GlifWriter *gw, const GlifGrid *grid);
int  glif_writer_finish(GlifWriter *gw);

/* Audio support (crushed PCM) */
int  glif_writer_enable_audio(GlifWriter *gw, int src_rate, int channels,
                              int dst_rate, int bit_depth);
void glif_writer_keep_original_audio(GlifWriter *gw, const uint8_t *opus_data,
                                     size_t len);
void glif_writer_audio_samples(GlifWriter *gw, const int16_t *pcm, int samples);

#endif
