#ifndef GLIF_COMPRESS_H
#define GLIF_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

/* Frame types — even = keyframe, odd = delta (P-frame).
 * Delta check: (type & 0x01) != 0 */
#define GLIF_FRAME_DEFLATE                  0x00
#define GLIF_FRAME_DELTA_DEFLATE            0x01
#define GLIF_FRAME_FILTERED_DEFLATE         0x02
#define GLIF_FRAME_DELTA_FILTERED_DEFLATE   0x03
#define GLIF_FRAME_PALETTE_DEFLATE          0x04
#define GLIF_FRAME_PLANAR_DEFLATE           0x06
#define GLIF_FRAME_DELTA_PLANAR_DEFLATE     0x07

/* Deflate (deinterleave + single deflate stream) */
size_t glif_compress_deflate_bound(int cells);
int glif_compress_deflate_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap);
int glif_compress_deflate_decode(const uint8_t *in, size_t len, uint8_t *out, int cells);
int glif_compress_delta_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                       int cells, uint8_t *work, uint8_t *out, size_t cap);
int glif_compress_delta_deflate_decode(const uint8_t *in, size_t len,
                                       const uint8_t *prev, uint8_t *work,
                                       uint8_t *out, int cells);

/* Filtered deflate (deinterleave + PNG-style adaptive row prediction + deflate) */
size_t glif_compress_filtered_deflate_bound(int cols, int rows);
int glif_compress_filtered_deflate_encode(const uint8_t *cur, int cols, int rows,
                                           uint8_t *out, size_t cap);
int glif_compress_filtered_deflate_decode(const uint8_t *in, size_t len,
                                           uint8_t *out, int cols, int rows);
int glif_compress_delta_filtered_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                                 int cols, int rows,
                                                 uint8_t *out, size_t cap);
int glif_compress_delta_filtered_deflate_decode(const uint8_t *in, size_t len,
                                                 const uint8_t *prev,
                                                 uint8_t *out, int cols, int rows);

/* Palette deflate (char + color palette + indices, deflated) */
size_t glif_compress_palette_deflate_bound(int cells);
int glif_compress_palette_deflate_encode(const uint8_t *cur, int cells,
                                          uint8_t *out, size_t cap);
int glif_compress_palette_deflate_decode(const uint8_t *in, size_t len,
                                          uint8_t *out, int cells);

/* Per-plane separate deflate (deflate each channel independently) */
size_t glif_compress_planar_deflate_bound(int cells);
int glif_compress_planar_deflate_encode(const uint8_t *cur, int cells,
                                         uint8_t *out, size_t cap);
int glif_compress_planar_deflate_decode(const uint8_t *in, size_t len,
                                         uint8_t *out, int cells);
int glif_compress_delta_planar_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                               int cells, uint8_t *out, size_t cap);
int glif_compress_delta_planar_deflate_decode(const uint8_t *in, size_t len,
                                               const uint8_t *prev,
                                               uint8_t *out, int cells);

#endif
