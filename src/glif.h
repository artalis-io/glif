#ifndef GLIF_GLIF_H
#define GLIF_GLIF_H

#include "compress.h"
#include <stddef.h>
#include <stdint.h>

/* Parsed header */
typedef struct {
    uint8_t version, flags;
    uint16_t cols, rows, cell_w, cell_h;
    float fps;
    uint32_t frames;
    uint8_t block_w, block_h;   /* v3: macroblock dimensions (0 = not used) */
} GlifHeader;

/* Per-frame index entry */
typedef struct {
    size_t offset;        /* byte offset of payload in buffer */
    uint8_t type;         /* GLIF_FRAME_RAW/RLE/DELTA/DELTA_RLE */
    uint32_t payload_len;
} GlifFrameIndex;

typedef struct {
    const uint8_t *data;
    size_t len;
    GlifHeader header;
    GlifFrameIndex *index;   /* header.frames entries */
    int cells;
    uint8_t *decoded;        /* current decoded frame, cells*4 */
    uint8_t *prev;           /* for delta decode */
    uint8_t *work;           /* scratch for delta+RLE */
    int cur_frame;           /* last decoded frame, -1 = none */
} GlifReader;

int glif_reader_open(GlifReader *gr, const uint8_t *data, size_t len);
int glif_reader_decode(GlifReader *gr, uint32_t frame);
const uint8_t *glif_reader_frame_data(const GlifReader *gr);
const GlifHeader *glif_reader_header(const GlifReader *gr);
void glif_reader_close(GlifReader *gr);

#endif
