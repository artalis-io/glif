"""Low-level cffi declarations for the Glif C library."""

import os
import cffi

ffi = cffi.FFI()

ffi.cdef("""
    /* vec6.h */
    typedef struct { float v[6]; } GlifVec6;
    typedef struct { float v[10]; } GlifVec10;

    /* image.h */
    typedef struct {
        int width;
        int height;
        int channels;
        uint8_t *pixels;
        int owns_pixels;
    } GlifImage;

    typedef struct {
        int width;
        int height;
        float *data;
    } GlifLightnessMap;

    int glif_image_load(GlifImage *img, const char *path);
    int glif_image_load_buffer(GlifImage *img, uint8_t *data, int w, int h, int channels);
    void glif_image_free(GlifImage *img);
    int glif_lightness_map_create(GlifLightnessMap *lm, const GlifImage *img);
    int glif_lightness_map_update(GlifLightnessMap *lm, const GlifImage *img);
    void glif_lightness_map_normalize(GlifLightnessMap *lm);
    void glif_lightness_map_free(GlifLightnessMap *lm);

    /* sampling.h */
    #define GLIF_NUM_INTERNAL 6
    #define GLIF_NUM_EXTERNAL 10
    #define GLIF_MAX_AFFECTING 4
    #define GLIF_NUM_CIRCLES 16

    typedef struct {
        float cx;
        float cy;
        float r;
    } GlifSamplingCircle;

    typedef struct {
        GlifSamplingCircle internal[6];
        GlifSamplingCircle external[10];
        int affecting[6][4];
        int affecting_count[6];
    } GlifSamplingConfig;

    void glif_sampling_config_init(GlifSamplingConfig *sc);
    float glif_sampling_circle_average(const GlifLightnessMap *lm, float cx_px, float cy_px, float r_px);

    typedef struct {
        int *offsets;
        int count;
        float inv_count;
        int min_dx;
        int max_dx;
        int min_dy;
        int max_dy;
    } GlifCircleMask;

    typedef struct {
        GlifCircleMask masks[16];
        int stride;
    } GlifPrecomputedMasks;

    int glif_sampling_precompute(GlifPrecomputedMasks *pm, const GlifSamplingConfig *sc,
                            int cell_w, int cell_h, int stride);
    void glif_sampling_precompute_free(GlifPrecomputedMasks *pm);

    /* grid.h */
    typedef struct {
        int px;
        int py;
        GlifVec6 shape;
        GlifVec10 external;
        uint8_t r, g, b;
        char ch;
    } GlifGridCell;

    typedef struct {
        int rows;
        int cols;
        int cell_w;
        int cell_h;
        GlifGridCell *cells;
    } GlifGrid;

    int glif_grid_create(GlifGrid *grid, const GlifImage *img, int cell_w, int cell_h);
    void glif_grid_compute_vectors(GlifGrid *grid, const GlifLightnessMap *lm,
                              const GlifSamplingConfig *sc);
    void glif_grid_compute_vectors_fast(GlifGrid *grid, const GlifLightnessMap *lm,
                                   const GlifPrecomputedMasks *pm);
    void glif_grid_compute_colors(GlifGrid *grid, const GlifImage *img);
    void glif_grid_free(GlifGrid *grid);

    /* font.h */
    #define GLIF_CHAR_FIRST 32
    #define GLIF_CHAR_LAST 126
    #define GLIF_CHAR_COUNT 95

    typedef struct {
        char ch;
        GlifVec6 shape;
        uint8_t *bitmap;
    } GlifCharEntry;

    typedef struct {
        GlifCharEntry entries[95];
        int cell_w;
        int cell_h;
        unsigned char *font_data;
        int owns_font_data;
    } GlifCharDatabase;

    int glif_char_db_create(GlifCharDatabase *db, const char *font_path,
                       int cell_w, int cell_h, const GlifSamplingConfig *sc);
    int glif_char_db_create_from_memory(GlifCharDatabase *db, const unsigned char *font_data,
                                   size_t font_len, int cell_w, int cell_h,
                                   const GlifSamplingConfig *sc);
    void glif_char_db_free(GlifCharDatabase *db);

    /* contrast.h */
    typedef struct {
        float floor;
        float ceil;
        float frame_avg;
        float frame_p10;
        float frame_p90;
    } GlifAdaptiveContrast;

    void glif_contrast_analyze_frame(GlifAdaptiveContrast *ac, const GlifGrid *grid);
    void glif_contrast_directional(GlifGrid *grid, const GlifSamplingConfig *sc,
                              float crunch, const GlifAdaptiveContrast *ac);
    void glif_contrast_global(GlifGrid *grid, float crunch, const GlifAdaptiveContrast *ac);

    /* match.h */
    void glif_match_grid(GlifGrid *grid, const GlifCharDatabase *db);
""")


def _find_library():
    """Find libglif shared library, searching common locations."""
    import sys

    if sys.platform == "darwin":
        name = "libglif.dylib"
    else:
        name = "libglif.so"

    # Search paths in order of preference
    search_dirs = []

    # 1. GLIF_LIB_DIR environment variable
    env_dir = os.environ.get("GLIF_LIB_DIR")
    if env_dir:
        search_dirs.append(env_dir)

    # 2. Project root (two levels up from bindings/python/)
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.normpath(os.path.join(pkg_dir, "..", "..", "..", ".."))
    search_dirs.append(project_root)

    # 3. Current working directory
    search_dirs.append(os.getcwd())

    for d in search_dirs:
        path = os.path.join(d, name)
        if os.path.isfile(path):
            return path

    # Fall back to system ld path
    return name


lib = ffi.dlopen(_find_library())
