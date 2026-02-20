#ifndef GLIF_COMPRESS_H
#define GLIF_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

/* Frame type flags (composable via bitwise OR) */
#define GLIF_FRAME_RAW             0x00
#define GLIF_FRAME_DELTA           0x01
#define GLIF_FRAME_RLE             0x02
#define GLIF_FRAME_DELTA_RLE       0x03
#define GLIF_FRAME_DEFLATE          0x04
#define GLIF_FRAME_PLANAR_DELTA_RLE 0x05
#define GLIF_FRAME_PLANAR_RLE       0x06
#define GLIF_FRAME_BLOCK_DELTA      0x07

#define GLIF_FRAME_DELTA_DEFLATE    0x09

#define GLIF_BLOCK_SIZE 8

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

/* Byte-level RLE (for single-channel planes) */
size_t glif_compress_byte_rle_bound(int len);
int glif_compress_byte_rle_encode(const uint8_t *in, int len, uint8_t *out, size_t cap);
int glif_compress_byte_rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, int len);

/* Planar codecs */
size_t glif_compress_planar_rle_bound(int cells);
int glif_compress_planar_rle_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap);
int glif_compress_planar_rle_decode(const uint8_t *in, size_t len, uint8_t *out, int cells);
int glif_compress_planar_delta_rle_encode(const uint8_t *cur, const uint8_t *prev,
                                          int cells, uint8_t *out, size_t cap);
int glif_compress_planar_delta_rle_decode(const uint8_t *in, size_t len,
                                          const uint8_t *prev, uint8_t *out, int cells);

/* Deflate codecs (zlib) */
size_t glif_compress_deflate_bound(int cells);
int glif_compress_deflate_encode(const uint8_t *cur, int cells, uint8_t *out, size_t cap);
int glif_compress_deflate_decode(const uint8_t *in, size_t len, uint8_t *out, int cells);
int glif_compress_delta_deflate_encode(const uint8_t *cur, const uint8_t *prev,
                                       int cells, uint8_t *work, uint8_t *out, size_t cap);
int glif_compress_delta_deflate_decode(const uint8_t *in, size_t len,
                                       const uint8_t *prev, uint8_t *work,
                                       uint8_t *out, int cells);

/* Block delta codec */
size_t glif_compress_block_delta_bound(int cols, int rows);
int glif_compress_block_delta_encode(const uint8_t *cur, const uint8_t *prev,
                                     int cols, int rows, uint8_t *out, size_t cap);
int glif_compress_block_delta_decode(const uint8_t *in, size_t len,
                                     const uint8_t *prev, uint8_t *out,
                                     int cols, int rows);

#endif
