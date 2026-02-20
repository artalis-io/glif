#ifndef GLIF_COMPRESS_H
#define GLIF_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

/* Frame type flags (composable via bitwise OR) */
#define GLIF_FRAME_RAW       0x00
#define GLIF_FRAME_DELTA     0x01
#define GLIF_FRAME_RLE       0x02
#define GLIF_FRAME_DELTA_RLE 0x03

/* Worst-case output sizes for pre-allocation */
size_t glif_compress_rle_bound(int cells);     /* cells * 5 */
size_t glif_compress_delta_bound(int cells);   /* 4 + cells * 6 */

/* Encode — return bytes written to out, or -1 on error */
int glif_compress_rle_encode(const uint8_t *cur, int cells,
                             uint8_t *out, size_t cap);
int glif_compress_delta_encode(const uint8_t *cur, const uint8_t *prev,
                               int cells, uint8_t *out, size_t cap);
int glif_compress_delta_rle_encode(const uint8_t *cur, const uint8_t *prev,
                                   int cells, uint8_t *work,
                                   uint8_t *out, size_t cap);

/* Decode — return 0 on success, -1 on corrupt data */
int glif_compress_rle_decode(const uint8_t *in, size_t len,
                             uint8_t *out, int cells);
int glif_compress_delta_decode(const uint8_t *in, size_t len,
                               const uint8_t *prev,
                               uint8_t *out, int cells);
int glif_compress_delta_rle_decode(const uint8_t *in, size_t len,
                                   const uint8_t *prev, uint8_t *work,
                                   uint8_t *out, int cells);

#endif
