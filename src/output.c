#include "output.h"
#include "blip.h"
#include "compress.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

void glif_output_plain(const GlifGrid *grid) {
    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            putchar(grid->cells[r * grid->cols + c].ch);
        }
        putchar('\n');
    }
}

void glif_output_ansi(const GlifGrid *grid) {
    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GlifGridCell *cell = &grid->cells[r * grid->cols + c];
            printf("\033[38;2;%d;%d;%dm%c",
                   cell->r, cell->g, cell->b, cell->ch);
        }
        printf("\033[0m\n");
    }
    printf("\033[0m");
}

int glif_frame_diff_init(GlifFrameDiff *fd, int rows, int cols) {
    size_t cells = (size_t)rows * (size_t)cols;
    if (cells > (size_t)INT_MAX) return -1;
    /* Worst case: every cell changed, ~48 bytes per cell + cursor moves */
    if (cells > (SIZE_MAX - 32 - (size_t)rows * 8) / 64) return -1;
    fd->bufsize = cells * 64 + (size_t)rows * 8 + 32;
    fd->buf = malloc(fd->bufsize);
    if (!fd->buf) return -1;
    fd->prev = calloc(cells, 4);  /* zeroed — first frame renders all */
    if (!fd->prev) { free(fd->buf); fd->buf = NULL; return -1; }
    fd->cells = (int)cells;
    fd->cols = cols;
    return 0;
}

/* Advance p by snprintf result, clamped to available space (never past end). */
static inline char *snprintf_adv(char *p, char *end, const char *fmt, ...) {
    size_t rem = (p < end) ? (size_t)(end - p) : 0;
    if (rem == 0) return p;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(p, rem, fmt, ap);
    va_end(ap);
    return (n > 0 && (size_t)n < rem) ? p + n : p + rem - 1;
}

/* Full redraw — cursor home, emit every cell sequentially. No cursor moves needed. */
static void frame_full_redraw(GlifFrameDiff *fd, const GlifGrid *grid, int dark_mode) {
    char *p = fd->buf;
    char *end = fd->buf + fd->bufsize;
    int rows = grid->rows;
    int cols = grid->cols;

    if (p + 3 > end) return;
    *p++ = '\033'; *p++ = '['; *p++ = 'H';

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const GlifGridCell *cell = &grid->cells[r * cols + c];
            if (dark_mode) {
                p = snprintf_adv(p, end, "\033[38;2;%d;%d;%dm%c",
                                 cell->r, cell->g, cell->b, cell->ch);
            } else {
                p = snprintf_adv(p, end, "\033[48;2;%d;%d;%dm\033[38;2;255;255;255m%c",
                                 cell->r, cell->g, cell->b, cell->ch);
            }
        }
        if (p + 5 > end) break;
        *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm'; *p++ = '\n';
    }
    if (p + 4 <= end) {
        *p++ = '\033'; *p++ = '['; *p++ = '0'; *p++ = 'm';
    }

    if (fwrite(fd->buf, 1, (size_t)(p - fd->buf), stdout) != (size_t)(p - fd->buf))
        return;
    fflush(stdout);
}

void glif_frame_diff_render(GlifFrameDiff *fd, const GlifGrid *grid, int dark_mode) {
    int rows = grid->rows;
    int cols = grid->cols;
    int total = rows * cols;

    /* Estimate byte cost of diff vs full redraw.
     * cell_cost: ~22 bytes (dark) or ~46 bytes (light) per cell
     * full: cursor_home(3) + total * cell_cost + rows * reset_newline(5)
     * diff: changed * cell_cost + jumps * cursor_move(~9) + reset(4)
     * A jump is needed when a changed cell isn't consecutive to the previous. */
    int cell_cost = dark_mode ? 22 : 46;
    int changed = 0, jumps = 0;
    int prev_idx = -2;  /* impossible value so first changed cell counts as jump */
    for (int i = 0; i < total; i++) {
        const GlifGridCell *cell = &grid->cells[i];
        uint8_t *pv = &fd->prev[i * 4];
        if (pv[0] != (uint8_t)cell->ch ||
            pv[1] != cell->r ||
            pv[2] != cell->g ||
            pv[3] != cell->b) {
            changed++;
            if (i != prev_idx + 1) jumps++;
            prev_idx = i;
        }
    }

    int full_cost = 3 + total * cell_cost + rows * 5;
    int diff_cost = changed * cell_cost + jumps * 9 + 4;
    if (diff_cost >= full_cost) {
        /* Update prev buffer */
        for (int i = 0; i < total; i++) {
            const GlifGridCell *cell = &grid->cells[i];
            uint8_t *prev = &fd->prev[i * 4];
            prev[0] = (uint8_t)cell->ch;
            prev[1] = cell->r;
            prev[2] = cell->g;
            prev[3] = cell->b;
        }
        frame_full_redraw(fd, grid, dark_mode);
        return;
    }

    /* Diff path — only emit changed cells */
    char *p = fd->buf;
    char *end = fd->buf + fd->bufsize;
    int cursor_r = -1, cursor_c = -1;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            const GlifGridCell *cell = &grid->cells[idx];
            uint8_t *prev = &fd->prev[idx * 4];

            if (prev[0] == (uint8_t)cell->ch &&
                prev[1] == cell->r &&
                prev[2] == cell->g &&
                prev[3] == cell->b) {
                continue;
            }

            prev[0] = (uint8_t)cell->ch;
            prev[1] = cell->r;
            prev[2] = cell->g;
            prev[3] = cell->b;

            if (cursor_r != r || cursor_c != c) {
                p = snprintf_adv(p, end, "\033[%d;%dH", r + 1, c + 1);
            }

            if (dark_mode) {
                p = snprintf_adv(p, end, "\033[38;2;%d;%d;%dm%c",
                                 cell->r, cell->g, cell->b, cell->ch);
            } else {
                p = snprintf_adv(p, end, "\033[48;2;%d;%d;%dm\033[38;2;255;255;255m%c",
                                 cell->r, cell->g, cell->b, cell->ch);
            }
            cursor_r = r;
            cursor_c = c + 1;
        }
    }

    if (p > fd->buf) {
        p = snprintf_adv(p, end, "\033[0m");
        if (fwrite(fd->buf, 1, (size_t)(p - fd->buf), stdout) != (size_t)(p - fd->buf))
            return;
        fflush(stdout);
    }
}

void glif_frame_diff_free(GlifFrameDiff *fd) {
    free(fd->buf);
    free(fd->prev);
    fd->buf = NULL;
    fd->prev = NULL;
}

int glif_output_ppm(const GlifGrid *grid, const GlifCharDatabase *db,
               const char *path, int scale, int dark_mode) {
    if (!grid || !grid->cells || !db || !path) return -1;
    if (scale < 1) scale = 1;
    if (scale > 64) scale = 64;
    int rw = grid->cell_w * scale;  /* render cell width */
    int rh = grid->cell_h * scale;  /* render cell height */
    size_t img_w = (size_t)grid->cols * (size_t)rw;
    size_t img_h = (size_t)grid->rows * (size_t)rh;

    /* Get high-res glyph bitmaps if scaled, otherwise use the analysis bitmaps */
    uint8_t **render_bmps = NULL;
    if (scale > 1) {
        render_bmps = glif_char_db_render_bitmaps(db, scale);
        if (!render_bmps) return -1;
    }

    uint8_t *pixels = calloc(img_w * img_h * 3, 1);
    if (!pixels) {
        if (render_bmps) {
            for (int i = 0; i < GLIF_CHAR_COUNT; i++) free(render_bmps[i]);
            free(render_bmps);
        }
        return -1;
    }

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GlifGridCell *cell = &grid->cells[r * grid->cols + c];

            int char_idx = cell->ch - GLIF_CHAR_FIRST;
            if (char_idx < 0 || char_idx >= GLIF_CHAR_COUNT) char_idx = 0;

            const uint8_t *bmp;
            int bw, bh;
            if (render_bmps) {
                bmp = render_bmps[char_idx];
                bw = rw;
                bh = rh;
            } else {
                bmp = db->entries[char_idx].bitmap;
                bw = grid->cell_w;
                bh = grid->cell_h;
            }

            int ox = c * rw;
            int oy = r * rh;

            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    size_t px = (size_t)(ox + x);
                    size_t py = (size_t)(oy + y);
                    if (px >= img_w || py >= img_h) continue;

                    float alpha = bmp ? bmp[y * bw + x] / 255.0f : 0.0f;
                    uint8_t *dst = pixels + (py * img_w + px) * 3;
                    if (dark_mode) {
                        /* Black background, colored glyphs */
                        dst[0] = (uint8_t)(cell->r * alpha);
                        dst[1] = (uint8_t)(cell->g * alpha);
                        dst[2] = (uint8_t)(cell->b * alpha);
                    } else {
                        /* Color background, white glyphs */
                        dst[0] = (uint8_t)(cell->r + (255 - cell->r) * alpha);
                        dst[1] = (uint8_t)(cell->g + (255 - cell->g) * alpha);
                        dst[2] = (uint8_t)(cell->b + (255 - cell->b) * alpha);
                    }
                }
            }
        }
    }

    if (render_bmps) {
        for (int i = 0; i < GLIF_CHAR_COUNT; i++) free(render_bmps[i]);
        free(render_bmps);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(pixels);
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }

    fprintf(f, "P6\n%zu %zu\n255\n", img_w, img_h);
    size_t expected = img_w * img_h * 3;
    size_t written = fwrite(pixels, 1, expected, f);
    fclose(f);
    free(pixels);
    if (written != expected) return -1;
    return 0;
}

int glif_ppm_pipe_init(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db,
                  int scale, int dark_mode) {
    if (scale < 1) scale = 1;
    if (scale > 64) scale = 64;
    pp->scale = scale;
    pp->dark_mode = dark_mode;
    pp->rw = grid->cell_w * scale;
    pp->rh = grid->cell_h * scale;
    pp->img_w = (size_t)grid->cols * (size_t)pp->rw;
    pp->img_h = (size_t)grid->rows * (size_t)pp->rh;

    pp->pixels = calloc(pp->img_w * pp->img_h, 3);
    if (!pp->pixels) return -1;

    pp->render_bmps = NULL;
    if (scale > 1) {
        pp->render_bmps = glif_char_db_render_bitmaps(db, scale);
        if (!pp->render_bmps) { free(pp->pixels); pp->pixels = NULL; return -1; }
    }
    return 0;
}

void glif_ppm_pipe_render(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db) {
    int rw = pp->rw, rh = pp->rh;
    size_t img_w = pp->img_w;
    int dark_mode = pp->dark_mode;
    uint8_t *pixels = pp->pixels;

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            const GlifGridCell *cell = &grid->cells[r * grid->cols + c];
            int char_idx = cell->ch - GLIF_CHAR_FIRST;
            if (char_idx < 0 || char_idx >= GLIF_CHAR_COUNT) char_idx = 0;

            const uint8_t *bmp;
            int bw, bh;
            if (pp->render_bmps) {
                bmp = pp->render_bmps[char_idx];
                bw = rw; bh = rh;
            } else {
                bmp = db->entries[char_idx].bitmap;
                bw = grid->cell_w; bh = grid->cell_h;
            }

            int ox = c * rw, oy = r * rh;
            for (int y = 0; y < bh; y++) {
                uint8_t *row = pixels + ((size_t)(oy + y) * img_w + (size_t)ox) * 3;
                for (int x = 0; x < bw; x++) {
                    float alpha = bmp ? bmp[y * bw + x] / 255.0f : 0.0f;
                    if (dark_mode) {
                        row[0] = (uint8_t)(cell->r * alpha);
                        row[1] = (uint8_t)(cell->g * alpha);
                        row[2] = (uint8_t)(cell->b * alpha);
                    } else {
                        row[0] = (uint8_t)(cell->r + (255 - cell->r) * alpha);
                        row[1] = (uint8_t)(cell->g + (255 - cell->g) * alpha);
                        row[2] = (uint8_t)(cell->b + (255 - cell->b) * alpha);
                    }
                    row += 3;
                }
            }
        }
    }
}

void glif_ppm_pipe_frame(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db) {
    glif_ppm_pipe_render(pp, grid, db);
    size_t nbytes = pp->img_w * pp->img_h * 3;
    fprintf(stdout, "P6\n%zu %zu\n255\n", pp->img_w, pp->img_h);
    if (fwrite(pp->pixels, 1, nbytes, stdout) != nbytes) return;
    fflush(stdout);
}

void glif_raw_pipe_frame(GlifPpmPipe *pp, const GlifGrid *grid, const GlifCharDatabase *db) {
    glif_ppm_pipe_render(pp, grid, db);
    size_t nbytes = pp->img_w * pp->img_h * 3;
    if (fwrite(pp->pixels, 1, nbytes, stdout) != nbytes) return;
    fflush(stdout);
}

void glif_ppm_pipe_free(GlifPpmPipe *pp) {
    free(pp->pixels);
    if (pp->render_bmps) {
        for (int i = 0; i < GLIF_CHAR_COUNT; i++) free(pp->render_bmps[i]);
        free(pp->render_bmps);
    }
    pp->pixels = NULL;
    pp->render_bmps = NULL;
}

/* ---- .glif binary format ---- */

static int write_u8(FILE *f, uint8_t v) {
    return fwrite(&v, 1, 1, f) == 1 ? 0 : -1;
}
static int write_u16(FILE *f, uint16_t v) {
    uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    return fwrite(buf, 1, 2, f) == 2 ? 0 : -1;
}
static int write_u32(FILE *f, uint32_t v) {
    uint8_t buf[4] = { (uint8_t)(v), (uint8_t)(v >> 8),
                        (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(buf, 1, 4, f) == 4 ? 0 : -1;
}
static int write_f32(FILE *f, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    return write_u32(f, bits);
}

int glif_writer_init(GlifWriter *gw, const char *path,
                     int cols, int rows, int cell_w, int cell_h,
                     float fps, int dark_mode) {
    return glif_writer_init_v2(gw, path, cols, rows, cell_w, cell_h,
                               fps, dark_mode, 0);
}

int glif_writer_init_v2(GlifWriter *gw, const char *path,
                        int cols, int rows, int cell_w, int cell_h,
                        float fps, int dark_mode, int compressed) {
    if (cols > 65535 || rows > 65535 || cell_w > 65535 || cell_h > 65535) {
        fprintf(stderr, "error: grid dimensions too large for .glif format\n");
        return -1;
    }
    gw->file = fopen(path, "wb");
    if (!gw->file) {
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }
    gw->frames = 0;
    size_t cell_count = (size_t)cols * (size_t)rows;
    if (cell_count > (size_t)INT_MAX) {
        fprintf(stderr, "error: grid too large (%d×%d)\n", cols, rows);
        fclose(gw->file); gw->file = NULL;
        return -1;
    }
    gw->cells = (int)cell_count;
    gw->cols = cols;
    gw->rows = rows;
    gw->err = 0;
    gw->compressed = compressed;
    gw->prev = NULL;
    gw->cur = NULL;
    gw->work = NULL;
    gw->enc_buf = NULL;
    gw->enc_cap = 0;
    gw->enc_buf2 = NULL;
    gw->enc_cap2 = 0;
    gw->keyframe_interval = 0;
    gw->quant_shift = 0;
    gw->threshold = 0;
    gw->blip = NULL;
    gw->orig_audio = NULL;
    gw->orig_audio_len = 0;
    gw->orig_codec = 0;

    uint8_t version = GLIF_VERSION;
    gw->flags = dark_mode ? GLIF_FLAG_DARK : 0;
    if (compressed) gw->flags |= GLIF_FLAG_COMPRESSED;
    uint8_t flags = gw->flags;

    /* Write header (24 bytes) */
    int e = 0;
    e |= (fwrite(GLIF_MAGIC, 1, 4, gw->file) != 4) ? -1 : 0;
    e |= write_u8(gw->file, version);
    e |= write_u8(gw->file, flags);
    e |= write_u16(gw->file, (uint16_t)cols);
    e |= write_u16(gw->file, (uint16_t)rows);
    e |= write_u16(gw->file, (uint16_t)cell_w);
    e |= write_u16(gw->file, (uint16_t)cell_h);
    e |= write_f32(gw->file, fps);
    e |= write_u32(gw->file, 0);                  /* frames (placeholder) */
    e |= write_u8(gw->file, 0);    /* reserved */
    e |= write_u8(gw->file, 0);
    if (e) { gw->err = 1; }

    /* Allocate compression buffers */
    if (compressed) {
        size_t buf_size = (size_t)gw->cells * 4;
        gw->prev = calloc(buf_size, 1);
        gw->cur = malloc(buf_size);
        gw->work = malloc(buf_size);
        /* Worst case across all deflate codecs */
        size_t deflate_b = glif_compress_deflate_bound(gw->cells);
        size_t filt_b = glif_compress_filtered_deflate_bound(cols, rows);
        size_t pal_b = glif_compress_palette_deflate_bound(gw->cells);
        size_t pld_b = glif_compress_planar_deflate_bound(gw->cells);
        gw->enc_cap = deflate_b;
        if (filt_b > gw->enc_cap) gw->enc_cap = filt_b;
        if (pal_b > gw->enc_cap) gw->enc_cap = pal_b;
        if (pld_b > gw->enc_cap) gw->enc_cap = pld_b;
        gw->enc_buf = malloc(gw->enc_cap);
        /* Second buffer so we can try multiple codecs without re-encoding */
        gw->enc_cap2 = gw->enc_cap;
        gw->enc_buf2 = malloc(gw->enc_cap2);
        if (!gw->prev || !gw->cur || !gw->work || !gw->enc_buf || !gw->enc_buf2) {
            fprintf(stderr, "error: failed to allocate compression buffers\n");
            free(gw->prev); free(gw->cur); free(gw->work);
            free(gw->enc_buf); free(gw->enc_buf2);
            gw->prev = gw->cur = gw->work = gw->enc_buf = gw->enc_buf2 = NULL;
            fclose(gw->file);
            gw->file = NULL;
            return -1;
        }
    }

    return 0;
}

void glif_writer_set_keyframe_interval(GlifWriter *gw, int interval) {
    gw->keyframe_interval = interval > 0 ? interval : 0;
}

void glif_writer_set_quant(GlifWriter *gw, int bits) {
    gw->quant_shift = (bits >= 1 && bits <= 4) ? bits : 0;
}

void glif_writer_set_threshold(GlifWriter *gw, int thresh) {
    gw->threshold = (thresh >= 0 && thresh <= 255) ? thresh : 0;
}

void glif_writer_frame(GlifWriter *gw, const GlifGrid *grid) {
    if (gw->err) return;
    int total = grid->rows * grid->cols;

    if (!gw->compressed) {
        /* v1 path: raw frame data */
        for (int i = 0; i < total; i++) {
            const GlifGridCell *cell = &grid->cells[i];
            uint8_t data[4] = { (uint8_t)cell->ch, cell->r, cell->g, cell->b };
            if (fwrite(data, 1, 4, gw->file) != 4) { gw->err = 1; return; }
        }
        gw->frames++;
        return;
    }

    /* Flatten grid to cur, try all encodings, pick smallest */
    for (int i = 0; i < total; i++) {
        const GlifGridCell *cell = &grid->cells[i];
        gw->cur[i * 4]     = (uint8_t)cell->ch;
        gw->cur[i * 4 + 1] = cell->r;
        gw->cur[i * 4 + 2] = cell->g;
        gw->cur[i * 4 + 3] = cell->b;
    }

    /* Quantize RGB channels */
    if (gw->quant_shift > 0) {
        uint8_t mask = (uint8_t)(0xFF << gw->quant_shift);
        for (int i = 0; i < total; i++) {
            gw->cur[i*4+1] &= mask;
            gw->cur[i*4+2] &= mask;
            gw->cur[i*4+3] &= mask;
        }
    }

    /* Temporal thresholding — snap near-identical cells to prev */
    if (gw->threshold > 0 && gw->frames > 0) {
        int t = gw->threshold;
        for (int i = 0; i < total; i++) {
            int o = i * 4;
            if (gw->cur[o] == gw->prev[o] &&
                abs(gw->cur[o+1] - gw->prev[o+1]) <= t &&
                abs(gw->cur[o+2] - gw->prev[o+2]) <= t &&
                abs(gw->cur[o+3] - gw->prev[o+3]) <= t) {
                memcpy(gw->cur + o, gw->prev + o, 4);
            }
        }
    }

    int best_size = 0x7FFFFFFF;
    uint8_t best_type = GLIF_FRAME_DEFLATE;
    const uint8_t *best_data = gw->enc_buf;

    /* Determine if this frame must be a keyframe */
    int force_keyframe = (gw->keyframe_interval > 0 &&
                          (gw->frames == 0 ||
                           (int)gw->frames % gw->keyframe_interval == 0));
    int allow_delta = (gw->frames > 0 && !force_keyframe);

    /* --- Keyframe codecs --- */

    /* Filtered deflate (usually best keyframe codec) */
    int fd_size = glif_compress_filtered_deflate_encode(gw->cur, gw->cols, gw->rows,
                                                         gw->enc_buf, gw->enc_cap);
    if (fd_size > 0 && fd_size < best_size) {
        best_size = fd_size;
        best_type = GLIF_FRAME_FILTERED_DEFLATE;
    }

    /* Plain deflate keyframe */
    int deflate_size = glif_compress_deflate_encode(gw->cur, total,
                                                    gw->enc_buf2, gw->enc_cap2);
    if (deflate_size > 0 && deflate_size < best_size) {
        best_size = deflate_size;
        best_type = GLIF_FRAME_DEFLATE;
    }

    /* Palette deflate (returns -1 if >256 unique colors) */
    int pal_size = glif_compress_palette_deflate_encode(gw->cur, total,
                                                         gw->enc_buf, gw->enc_cap);
    if (pal_size > 0 && pal_size < best_size) {
        best_size = pal_size;
        best_type = GLIF_FRAME_PALETTE_DEFLATE;
    }

    /* Per-plane separate deflate */
    int pld_size = glif_compress_planar_deflate_encode(gw->cur, total,
                                                        gw->enc_buf2, gw->enc_cap2);
    if (pld_size > 0 && pld_size < best_size) {
        best_size = pld_size;
        best_type = GLIF_FRAME_PLANAR_DEFLATE;
    }

    /* --- Delta codecs (only after first frame, not on forced keyframes) --- */
    if (allow_delta) {
        /* Delta + filtered deflate (usually best P-frame codec) */
        int dfd_size = glif_compress_delta_filtered_deflate_encode(
            gw->cur, gw->prev, gw->cols, gw->rows, gw->enc_buf, gw->enc_cap);
        if (dfd_size > 0 && dfd_size < best_size) {
            best_size = dfd_size;
            best_type = GLIF_FRAME_DELTA_FILTERED_DEFLATE;
        }

        /* Delta + deflate */
        int dd_size = glif_compress_delta_deflate_encode(gw->cur, gw->prev, total,
                                                          gw->work, gw->enc_buf2,
                                                          gw->enc_cap2);
        if (dd_size > 0 && dd_size < best_size) {
            best_size = dd_size;
            best_type = GLIF_FRAME_DELTA_DEFLATE;
        }

        /* Delta + per-plane separate deflate */
        int dpd_size = glif_compress_delta_planar_deflate_encode(gw->cur, gw->prev, total,
                                                                  gw->enc_buf, gw->enc_cap);
        if (dpd_size > 0 && dpd_size < best_size) {
            best_size = dpd_size;
            best_type = GLIF_FRAME_DELTA_PLANAR_DEFLATE;
        }
    }

    /* Re-encode the winner into enc_buf (the selection loop may have
     * overwritten the buffer that held the winning encoding) */
    int re_size = -1;
    switch (best_type) {
    case GLIF_FRAME_FILTERED_DEFLATE:
        re_size = glif_compress_filtered_deflate_encode(gw->cur, gw->cols, gw->rows,
                                                        gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_DEFLATE:
        re_size = glif_compress_deflate_encode(gw->cur, total, gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_PALETTE_DEFLATE:
        re_size = glif_compress_palette_deflate_encode(gw->cur, total, gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_PLANAR_DEFLATE:
        re_size = glif_compress_planar_deflate_encode(gw->cur, total, gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_DELTA_FILTERED_DEFLATE:
        re_size = glif_compress_delta_filtered_deflate_encode(gw->cur, gw->prev, gw->cols, gw->rows,
                                                              gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_DELTA_DEFLATE:
        re_size = glif_compress_delta_deflate_encode(gw->cur, gw->prev, total,
                                                     gw->work, gw->enc_buf, gw->enc_cap);
        break;
    case GLIF_FRAME_DELTA_PLANAR_DEFLATE:
        re_size = glif_compress_delta_planar_deflate_encode(gw->cur, gw->prev, total,
                                                            gw->enc_buf, gw->enc_cap);
        break;
    default:
        break;
    }
    if (re_size <= 0) { gw->err = 1; return; }
    best_data = gw->enc_buf;
    best_size = re_size;

    /* Write 5-byte frame envelope: [type(u8), payload_size(u32 LE)] */
    if (write_u8(gw->file, best_type) != 0) { gw->err = 1; return; }
    if (write_u32(gw->file, (uint32_t)best_size) != 0) { gw->err = 1; return; }

    /* Write payload */
    if (fwrite(best_data, 1, (size_t)best_size, gw->file) != (size_t)best_size) {
        gw->err = 1;
        return;
    }

    /* Copy cur to prev for next frame's delta */
    memcpy(gw->prev, gw->cur, (size_t)total * 4);
    gw->frames++;
}

/* Write BLIP v2 section — crushed PCM, deflate compressed.
 * Format (16-byte header + compressed data):
 *   0:  "BLIP"       (4 bytes)
 *   4:  version=2    (uint8)
 *   5:  bit_depth    (uint8)
 *   6:  sample_rate  (uint16 LE)
 *   8:  num_samples  (uint32 LE)
 *   12: data_size    (uint32 LE, compressed)
 *   16: [compressed PCM data] */
static void write_blip_section(GlifWriter *gw) {
    uint32_t num_samples = blip_encoder_samples(gw->blip);
    if (num_samples == 0) return;

    const uint8_t *pcm_data = blip_encoder_data(gw->blip);

    /* Prepare raw data (pack 4-bit if needed) */
    const uint8_t *raw = pcm_data;
    uint32_t raw_size = num_samples;
    uint8_t *packed = NULL;

    if (blip_encoder_bit_depth(gw->blip) == 4) {
        raw_size = (num_samples + 1) / 2;
        packed = malloc(raw_size);
        if (!packed) { gw->err = 1; return; }
        for (uint32_t i = 0; i < num_samples; i += 2) {
            uint8_t hi = pcm_data[i] & 0x0F;
            uint8_t lo = (i + 1 < num_samples) ? (pcm_data[i + 1] & 0x0F) : 0;
            packed[i / 2] = (uint8_t)((hi << 4) | lo);
        }
        raw = packed;
    }

    /* Deflate compress */
    mz_ulong comp_bound = mz_compressBound((mz_ulong)raw_size);
    uint8_t *comp = malloc(comp_bound);
    if (!comp) { free(packed); gw->err = 1; return; }
    mz_ulong comp_len = comp_bound;
    int mz_ret = mz_compress(comp, &comp_len, raw, (mz_ulong)raw_size);
    free(packed);
    if (mz_ret != MZ_OK) { free(comp); gw->err = 1; return; }

    /* Write 16-byte BLIP header + compressed data */
    int e = 0;
    e |= (fwrite(BLIP_MAGIC, 1, 4, gw->file) != 4) ? -1 : 0;
    e |= write_u8(gw->file, BLIP_VERSION);
    e |= write_u8(gw->file, (uint8_t)blip_encoder_bit_depth(gw->blip));
    e |= write_u16(gw->file, (uint16_t)blip_encoder_dst_rate(gw->blip));
    e |= write_u32(gw->file, num_samples);
    e |= write_u32(gw->file, (uint32_t)comp_len);
    if (!e) {
        if (fwrite(comp, 1, (size_t)comp_len, gw->file) != (size_t)comp_len)
            e = -1;
    }
    free(comp);
    if (e) gw->err = 1;
}

static void write_orig_section(GlifWriter *gw) {
    if (!gw->orig_audio || gw->orig_audio_len == 0) return;

    /* ORIG section header (12 bytes) */
    int e = 0;
    e |= (fwrite("ORIG", 1, 4, gw->file) != 4) ? -1 : 0;
    e |= write_u32(gw->file, gw->orig_codec);
    e |= write_u32(gw->file, (uint32_t)gw->orig_audio_len);
    if (e) { gw->err = 1; return; }

    /* Raw bitstream */
    if (fwrite(gw->orig_audio, 1, gw->orig_audio_len, gw->file) != gw->orig_audio_len) {
        gw->err = 1;
    }
}

int glif_writer_finish(GlifWriter *gw) {
    if (!gw->file) return -1;

    /* Write BLIP audio section after video frames */
    if (gw->blip && blip_encoder_samples(gw->blip) > 0) {
        write_blip_section(gw);
    }

    /* Write ORIG section if original audio is kept */
    if (gw->orig_audio && gw->orig_audio_len > 0) {
        write_orig_section(gw);
    }

    /* Seek back to flags field (offset 5) to set audio flag if needed */
    if (gw->blip && blip_encoder_samples(gw->blip) > 0) {
        gw->flags |= GLIF_FLAG_AUDIO;
        if (fseek(gw->file, 5, SEEK_SET) == 0) {
            if (write_u8(gw->file, gw->flags) != 0) gw->err = 1;
        }
    }

    /* Seek back to frames field (offset 18) and write actual count */
    if (fseek(gw->file, 18, SEEK_SET) == 0) {
        if (write_u32(gw->file, gw->frames) != 0) gw->err = 1;
    }

    fclose(gw->file);
    gw->file = NULL;

    /* Free compression buffers */
    free(gw->prev);     gw->prev = NULL;
    free(gw->cur);      gw->cur = NULL;
    free(gw->work);     gw->work = NULL;
    free(gw->enc_buf);  gw->enc_buf = NULL;
    free(gw->enc_buf2); gw->enc_buf2 = NULL;

    /* Free audio resources */
    if (gw->blip) {
        blip_encoder_free(gw->blip);
        free(gw->blip);
        gw->blip = NULL;
    }
    free(gw->orig_audio);   gw->orig_audio = NULL;

    return gw->err ? -1 : 0;
}

/* ── Audio support (crushed PCM) ── */

int glif_writer_enable_audio(GlifWriter *gw, int src_rate, int channels,
                             int dst_rate, int bit_depth) {
    if (!gw || gw->blip) return -1;

    gw->blip = calloc(1, sizeof(BlipEncoder));
    if (!gw->blip) return -1;

    if (blip_encoder_init(gw->blip, src_rate, channels, dst_rate, bit_depth) != 0) {
        free(gw->blip);
        gw->blip = NULL;
        return -1;
    }

    return 0;
}

void glif_writer_keep_original_audio(GlifWriter *gw, const uint8_t *opus_data,
                                     size_t len) {
    if (!gw || !opus_data || len == 0) return;
    gw->orig_audio = malloc(len);
    if (!gw->orig_audio) return;
    memcpy(gw->orig_audio, opus_data, len);
    gw->orig_audio_len = len;
    gw->orig_codec = 0x5355504F; /* "OPUS" in LE */
}

void glif_writer_audio_samples(GlifWriter *gw, const int16_t *pcm, int samples) {
    if (!gw || !gw->blip || !pcm || samples <= 0) return;
    blip_encoder_feed(gw->blip, pcm, samples);
}
