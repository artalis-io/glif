#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
#include "output.h"
#include "blip.h"
#ifdef __linux__
#include "platform/linux/v4l2_output.h"
#endif

typedef struct {
    const char *input_path;
    const char *font_path;
    const char *output_path;  /* NULL = terminal */
    int cell_w;
    int cell_h;
    float dir_crunch;
    float global_crunch;
    int color;  /* ANSI truecolor */
    int scale;  /* PPM render scale */
    int auto_fit;  /* auto-fit to terminal size */
    int dark_mode; /* PPM: black bg + colored glyphs */
    int video;     /* video mode: read raw RGB frames from stdin */
    int video_w;   /* video frame width */
    int video_h;   /* video frame height */
    float fps;     /* target fps (0 = unlimited) */
    int pipe_ppm;  /* video: write PPM frames to stdout instead of ANSI */
    int pipe_raw;  /* video: write raw RGB24 frames to stdout (no headers) */
    const char *glif_path; /* video: write .glif binary file */
    const char *v4l2_device; /* video: write to v4l2loopback device (Linux) */
    int compress;  /* video: enable v2 compressed .glif output */
    int quant_bits;    /* RGB bits to keep (4-8, 8=off) */
    int threshold;     /* temporal snap threshold (0=off) */
    GlifAdaptiveContrast adaptive; /* adaptive contrast params */
    int audio;             /* enable crushed PCM audio encoding */
    const char *keep_audio_path; /* path to original audio file (Opus/OGG/AAC) */
    int audio_rate;        /* output sample rate for crushed PCM (default: 8000) */
    int audio_depth;       /* output bit depth (4 or 8, default: 8) */
    const char *audio_pcm_path; /* path to raw PCM file (s16le, mono, 44100) */
} Config;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <input.png> [options]\n"
        "       %s --video <W> <H> [options]    (read raw RGB24 from stdin)\n"
        "\n"
        "Options:\n"
        "  -f, --font <path>        Monospace TTF (required)\n"
        "  -w, --cell-width <px>    Cell width (default: 10)\n"
        "  -h, --cell-height <px>   Cell height (default: 20)\n"
        "  -d, --dir-crunch <f>     Directional contrast crunch (default: 1.25)\n"
        "  -g, --global-crunch <f>  Global contrast crunch (default: 1.5)\n"
        "  -c, --color              ANSI truecolor terminal output\n"
        "  -o, --output <file>      Write PPM (.ppm) or SVG (.svg) image file\n"
        "  -a, --auto-fit           Fit output to terminal size\n"
        "  -s, --scale <n>          PPM render scale (default: 4)\n"
        "  --dark                   PPM: black bg + colored glyphs\n"
        "  --adaptive               Adaptive contrast (per-frame, less crunch in shadows)\n"
        "  --adapt-floor <0-255>    Noise floor relative to frame range (default: 5)\n"
        "  --adapt-ceil <0-255>     Midtone ceiling relative to frame range (default: 80)\n"
        "  --video <W> <H>          Video mode: read raw RGB24 frames from stdin\n"
        "  --fps <n>                Target framerate for video mode (default: 30)\n"
        "  --pipe-ppm               Video: write PPM frames to stdout (for ffmpeg)\n"
        "  --pipe-raw               Video: write raw RGB24 frames to stdout (no headers)\n"
        "  --v4l2 <device>          Video: write to v4l2loopback device (Linux only)\n"
        "  --output-glif <path>     Video: write .glif binary capture file\n"
        "  --compress               Enable v2 RLE+delta compression for .glif output\n"
        "  --quant <bits>           RGB bits to keep (4-8, default 6 with --compress, 8=off)\n"
        "  --threshold <n>          Temporal snap threshold (0-255, default 4 with --compress)\n"
        "  --audio                  Enable crushed PCM audio (requires --output-glif)\n"
        "  --keep-audio <path>      Embed original audio file (Opus/OGG/AAC) in .glif\n"
        "  --audio-rate <hz>        Output sample rate (default: 8000)\n"
        "  --audio-depth <bits>     Output bit depth, 4 or 8 (default: 8)\n"
        "  --audio-pcm <path>       Path to raw PCM file (s16le, mono, 44100)\n"
        "  --help                   Show this message\n",
        prog, prog);
}

static int parse_args(Config *cfg, int argc, char **argv) {
    cfg->input_path = NULL;
    cfg->font_path = NULL;
    cfg->output_path = NULL;
    cfg->cell_w = 10;
    cfg->cell_h = 20;
    cfg->dir_crunch = 1.25f;
    cfg->global_crunch = 1.5f;
    cfg->color = 0;
    cfg->scale = 4;
    cfg->auto_fit = 0;
    cfg->dark_mode = 0;
    cfg->video = 0;
    cfg->video_w = 0;
    cfg->video_h = 0;
    cfg->fps = 30.0f;
    cfg->pipe_ppm = 0;
    cfg->pipe_raw = 0;
    cfg->glif_path = NULL;
    cfg->v4l2_device = NULL;
    cfg->compress = 0;
    cfg->quant_bits = -1;
    cfg->threshold = -1;
    cfg->adaptive.floor = -1.0f;  /* disabled by default */
    cfg->adaptive.ceil = 80.0f / 255.0f;
    cfg->audio = 0;
    cfg->keep_audio_path = NULL;
    cfg->audio_rate = 8000;
    cfg->audio_depth = 8;
    cfg->audio_pcm_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--font") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -f requires argument\n"); return -1; }
            cfg->font_path = argv[i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--cell-width") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -w requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 2 || val > 10000) {
                fprintf(stderr, "error: invalid cell width '%s'\n", argv[i]); return -1;
            }
            cfg->cell_w = (int)val;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--cell-height") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -h requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 2 || val > 10000) {
                fprintf(stderr, "error: invalid cell height '%s'\n", argv[i]); return -1;
            }
            cfg->cell_h = (int)val;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir-crunch") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -d requires argument\n"); return -1; }
            char *end;
            float val = strtof(argv[i], &end);
            if (end == argv[i] || val < 0.0f) {
                fprintf(stderr, "error: invalid dir-crunch '%s'\n", argv[i]); return -1;
            }
            cfg->dir_crunch = val;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--global-crunch") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -g requires argument\n"); return -1; }
            char *end;
            float val = strtof(argv[i], &end);
            if (end == argv[i] || val < 0.0f) {
                fprintf(stderr, "error: invalid global-crunch '%s'\n", argv[i]); return -1;
            }
            cfg->global_crunch = val;
        } else if (strcmp(argv[i], "--video") == 0) {
            if (i + 2 >= argc) { fprintf(stderr, "error: --video requires W H\n"); return -1; }
            char *end;
            long w = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || w < 4 || w > 10000) {
                fprintf(stderr, "error: invalid video width '%s'\n", argv[i]); return -1;
            }
            long h = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || h < 4 || h > 10000) {
                fprintf(stderr, "error: invalid video height '%s'\n", argv[i]); return -1;
            }
            cfg->video = 1;
            cfg->video_w = (int)w;
            cfg->video_h = (int)h;
        } else if (strcmp(argv[i], "--fps") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --fps requires argument\n"); return -1; }
            char *end;
            float val = strtof(argv[i], &end);
            if (end == argv[i] || val < 0.0f || val > 1000.0f) {
                fprintf(stderr, "error: invalid fps '%s'\n", argv[i]); return -1;
            }
            cfg->fps = val;
        } else if (strcmp(argv[i], "--dark") == 0) {
            cfg->dark_mode = 1;
        } else if (strcmp(argv[i], "--adaptive") == 0) {
            cfg->adaptive.floor = 5.0f / 255.0f;
        } else if (strcmp(argv[i], "--adapt-floor") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --adapt-floor requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 0 || val > 255) {
                fprintf(stderr, "error: invalid adapt-floor '%s' (must be 0-255)\n", argv[i]); return -1;
            }
            cfg->adaptive.floor = (float)val / 255.0f;
        } else if (strcmp(argv[i], "--adapt-ceil") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --adapt-ceil requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 0 || val > 255) {
                fprintf(stderr, "error: invalid adapt-ceil '%s' (must be 0-255)\n", argv[i]); return -1;
            }
            cfg->adaptive.ceil = (float)val / 255.0f;
            if (cfg->adaptive.floor < 0.0f) cfg->adaptive.floor = 5.0f / 255.0f;
        } else if (strcmp(argv[i], "--pipe-ppm") == 0) {
            cfg->pipe_ppm = 1;
        } else if (strcmp(argv[i], "--pipe-raw") == 0) {
            cfg->pipe_raw = 1;
        } else if (strcmp(argv[i], "--v4l2") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --v4l2 requires device path\n"); return -1; }
            cfg->v4l2_device = argv[i];
        } else if (strcmp(argv[i], "--output-glif") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --output-glif requires path\n"); return -1; }
            cfg->glif_path = argv[i];
        } else if (strcmp(argv[i], "--compress") == 0) {
            cfg->compress = 1;
        } else if (strcmp(argv[i], "--quant") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --quant requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 4 || val > 8) {
                fprintf(stderr, "error: invalid quant '%s' (must be 4-8)\n", argv[i]); return -1;
            }
            cfg->quant_bits = (int)val;
        } else if (strcmp(argv[i], "--threshold") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --threshold requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 0 || val > 255) {
                fprintf(stderr, "error: invalid threshold '%s' (must be 0-255)\n", argv[i]); return -1;
            }
            cfg->threshold = (int)val;
        } else if (strcmp(argv[i], "--audio") == 0) {
            cfg->audio = 1;
        } else if (strcmp(argv[i], "--keep-audio") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --keep-audio requires path\n"); return -1; }
            cfg->keep_audio_path = argv[i];
        } else if (strcmp(argv[i], "--audio-rate") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --audio-rate requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || val < 2000 || val > 48000) {
                fprintf(stderr, "error: invalid audio-rate '%s' (2000-48000)\n", argv[i]); return -1;
            }
            cfg->audio_rate = (int)val;
        } else if (strcmp(argv[i], "--audio-depth") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --audio-depth requires argument\n"); return -1; }
            char *end2;
            long val = strtol(argv[i], &end2, 10);
            if (val != 4 && val != 8) {
                fprintf(stderr, "error: --audio-depth must be 4 or 8\n"); return -1;
            }
            cfg->audio_depth = (int)val;
        } else if (strcmp(argv[i], "--audio-pcm") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --audio-pcm requires argument\n"); return -1; }
            cfg->audio_pcm_path = argv[i];
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto-fit") == 0) {
            cfg->auto_fit = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) {
            cfg->color = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -o requires argument\n"); return -1; }
            cfg->output_path = argv[i];
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -s requires argument\n"); return -1; }
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || val < 1 || val > 64) {
                fprintf(stderr, "error: invalid scale '%s' (must be 1-64)\n", argv[i]); return -1;
            }
            cfg->scale = (int)val;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return -1;
        } else {
            if (cfg->input_path) {
                fprintf(stderr, "error: multiple input files\n");
                return -1;
            }
            cfg->input_path = argv[i];
        }
    }

    if (!cfg->input_path && !cfg->video) {
        fprintf(stderr, "error: no input file specified\n");
        return -1;
    }
    if (!cfg->font_path) {
        fprintf(stderr, "error: no font specified (use -f)\n");
        return -1;
    }
    if (cfg->cell_w < 2 || cfg->cell_h < 2 ||
        cfg->cell_w > 10000 || cfg->cell_h > 10000) {
        fprintf(stderr, "error: cell dimensions must be 2-10000\n");
        return -1;
    }
    if (cfg->pipe_ppm && cfg->pipe_raw) {
        fprintf(stderr, "error: --pipe-ppm and --pipe-raw are mutually exclusive\n");
        return -1;
    }
#ifndef __linux__
    if (cfg->v4l2_device) {
        fprintf(stderr, "error: --v4l2 output is Linux-only\n");
        return -1;
    }
#endif
    if (cfg->compress && !cfg->glif_path) {
        fprintf(stderr, "error: --compress requires --output-glif\n");
        return -1;
    }
    /* Apply lossy preprocessing defaults when --compress is active */
    if (cfg->compress) {
        if (cfg->quant_bits < 0) cfg->quant_bits = 6;
        if (cfg->threshold < 0) cfg->threshold = 4;
    } else {
        if (cfg->quant_bits < 0) cfg->quant_bits = 8;
        if (cfg->threshold < 0) cfg->threshold = 0;
    }
    if (cfg->quant_bits < 8 && !cfg->compress) {
        fprintf(stderr, "error: --quant requires --compress\n");
        return -1;
    }
    if (cfg->threshold > 0 && !cfg->compress) {
        fprintf(stderr, "error: --threshold requires --compress\n");
        return -1;
    }
    if (cfg->audio && !cfg->glif_path) {
        fprintf(stderr, "error: --audio requires --output-glif\n");
        return -1;
    }
    if (cfg->keep_audio_path && !cfg->glif_path) {
        fprintf(stderr, "error: --keep-audio requires --output-glif\n");
        return -1;
    }
    if (cfg->audio && !cfg->audio_pcm_path) {
        fprintf(stderr, "error: --audio requires --audio-pcm <path>\n");
        return -1;
    }
    return 0;
}

#ifndef __EMSCRIPTEN__
static double time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

static int run_image(Config *cfg) {
    /* 1. Load image */
    GlifImage img;
    if (glif_image_load(&img, cfg->input_path) != 0)
        return 1;

#ifndef __EMSCRIPTEN__
    /* 1b. Auto-fit cell size to terminal */
    if (cfg->auto_fit) {
        struct winsize ws;
        if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_col > 0 && ws.ws_row > 0) {
            cfg->cell_w = (img.width + ws.ws_col - 1) / ws.ws_col;
            cfg->cell_h = (img.height + ws.ws_row - 1) / ws.ws_row;
            if (cfg->cell_w < 2) cfg->cell_w = 2;
            if (cfg->cell_h < 2) cfg->cell_h = 2;
            fprintf(stderr, "Auto-fit: %dx%d terminal, cell size %dx%d\n",
                    ws.ws_col, ws.ws_row, cfg->cell_w, cfg->cell_h);
        } else {
            fprintf(stderr, "warning: cannot detect terminal size, using defaults\n");
        }
    }
#endif

    /* 2. Compute lightness map */
    GlifLightnessMap lm = { .data = NULL };
    if (glif_lightness_map_create(&lm, &img) != 0) {
        fprintf(stderr, "error: failed to create lightness map\n");
        glif_image_free(&img);
        return 1;
    }

    /* 3. Init sampling geometry */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifPrecomputedMasks pm;
    if (glif_sampling_precompute(&pm, &sc, cfg->cell_w, cfg->cell_h, lm.width) != 0) {
        fprintf(stderr, "error: failed to precompute sampling masks\n");
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        return 1;
    }

    /* 4. Load font + precompute character shape vectors */
    GlifCharDatabase db;
    if (glif_char_db_create(&db, cfg->font_path, cfg->cell_w, cfg->cell_h, &sc) != 0) {
        glif_sampling_precompute_free(&pm);
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        return 1;
    }

    /* 5. Create grid */
    GlifGrid grid;
    if (glif_grid_create(&grid, &img, cfg->cell_w, cfg->cell_h) != 0) {
        fprintf(stderr, "error: image too small for cell size %dx%d\n",
                cfg->cell_w, cfg->cell_h);
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        return 1;
    }

    /* 6–10. Pipeline */
    if (cfg->adaptive.floor >= 0.0f)
        glif_lightness_map_normalize(&lm);
    glif_grid_compute_vectors_fast(&grid, &lm, &pm);
    glif_grid_compute_colors(&grid, &img);
    glif_contrast_analyze_frame(&cfg->adaptive, &grid);
    glif_contrast_directional(&grid, &sc, cfg->dir_crunch, &cfg->adaptive);
    glif_contrast_global(&grid, cfg->global_crunch, &cfg->adaptive);
    glif_match_grid(&grid, &db);

    /* 11. Output */
    if (cfg->output_path) {
        /* Detect SVG by extension */
        size_t path_len = strlen(cfg->output_path);
        int is_svg = (path_len >= 4 &&
                      strcmp(cfg->output_path + path_len - 4, ".svg") == 0);
        if (is_svg) {
            if (glif_output_svg(&grid, &db, cfg->output_path, cfg->dark_mode) != 0) {
                fprintf(stderr, "error: failed to write SVG file\n");
            } else {
                fprintf(stderr, "Wrote %s (%dx%d grid, %dx%d cells)\n",
                        cfg->output_path, grid.cols, grid.rows,
                        grid.cell_w, grid.cell_h);
            }
        } else if (glif_output_ppm(&grid, &db, cfg->output_path, cfg->scale, cfg->dark_mode) != 0) {
            fprintf(stderr, "error: failed to write PPM file\n");
        } else {
            fprintf(stderr, "Wrote %s (%zux%zu)\n", cfg->output_path,
                    (size_t)grid.cols * (size_t)cfg->cell_w * (size_t)cfg->scale,
                    (size_t)grid.rows * (size_t)cfg->cell_h * (size_t)cfg->scale);
        }
    } else if (cfg->color) {
        glif_output_ansi(&grid);
    } else {
        glif_output_plain(&grid);
    }

    glif_grid_free(&grid);
    glif_char_db_free(&db);
    glif_sampling_precompute_free(&pm);
    glif_lightness_map_free(&lm);
    glif_image_free(&img);
    return 0;
}

static int run_video(Config *cfg) {
    int w = cfg->video_w;
    int h = cfg->video_h;
    size_t frame_size = (size_t)w * (size_t)h * 3;

#ifndef __EMSCRIPTEN__
    /* Auto-fit cell size to terminal if requested */
    if (cfg->auto_fit) {
        struct winsize ws;
        if (isatty(STDERR_FILENO) && ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_col > 0 && ws.ws_row > 0) {
            cfg->cell_w = (w + ws.ws_col - 1) / ws.ws_col;
            cfg->cell_h = (h + ws.ws_row - 2) / (ws.ws_row - 1);
            if (cfg->cell_w < 2) cfg->cell_w = 2;
            if (cfg->cell_h < 2) cfg->cell_h = 2;
            fprintf(stderr, "Auto-fit: %dx%d terminal, cell size %dx%d\n",
                    ws.ws_col, ws.ws_row, cfg->cell_w, cfg->cell_h);
        }
    }
#endif

    /* One-time init */
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifPrecomputedMasks pm;
    if (glif_sampling_precompute(&pm, &sc, cfg->cell_w, cfg->cell_h, w) != 0) {
        fprintf(stderr, "error: failed to precompute sampling masks\n");
        return 1;
    }

    GlifCharDatabase db;
    if (glif_char_db_create(&db, cfg->font_path, cfg->cell_w, cfg->cell_h, &sc) != 0) {
        glif_sampling_precompute_free(&pm);
        return 1;
    }

    /* Allocate frame buffer and create grid */
    uint8_t *frame_buf = malloc(frame_size);
    if (!frame_buf) {
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        return 1;
    }

    GlifImage img = { .width = w, .height = h, .channels = 3, .pixels = frame_buf, .owns_pixels = 0 };
    GlifGrid grid;
    if (glif_grid_create(&grid, &img, cfg->cell_w, cfg->cell_h) != 0) {
        fprintf(stderr, "error: video %dx%d too small for cell size %dx%d\n",
                w, h, cfg->cell_w, cfg->cell_h);
        free(frame_buf);
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        return 1;
    }

    /* Lightness map — allocated once, reused per frame */
    GlifLightnessMap lm;
    lm.width = w;
    lm.height = h;
    size_t npix = (size_t)w * (size_t)h;
    if (npix > SIZE_MAX / sizeof(float)) {
        glif_grid_free(&grid);
        free(frame_buf);
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        return 1;
    }
    lm.data = calloc(npix, sizeof(float));
    if (!lm.data) {
        glif_grid_free(&grid);
        free(frame_buf);
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        return 1;
    }

#ifndef __EMSCRIPTEN__
    double frame_interval = cfg->fps > 0.0f ? 1.0 / (double)cfg->fps : 0.0;
#endif
    int cols = grid.cols;
    int rows = grid.rows;

    /* Init renderers */
    GlifFrameDiff fd = {0};
    GlifPpmPipe pp = {0};
    GlifWriter gw = {0};
#ifdef __linux__
    V4l2Output vo = {0};
#endif
    FILE *audio_file = NULL;
    int16_t *audio_buf = NULL;
    int audio_samples_per_frame = 0;

    if (cfg->glif_path) {
        if (glif_writer_init_v2(&gw, cfg->glif_path, cols, rows,
                                cfg->cell_w, cfg->cell_h,
                                cfg->fps, cfg->dark_mode,
                                cfg->compress) != 0) {
            free(lm.data);
            glif_grid_free(&grid);
            free(frame_buf);
            glif_char_db_free(&db);
            glif_sampling_precompute_free(&pm);
            return 1;
        }
        if (cfg->quant_bits < 8)
            glif_writer_set_quant(&gw, 8 - cfg->quant_bits);
        if (cfg->threshold > 0)
            glif_writer_set_threshold(&gw, cfg->threshold);

        /* Enable crushed PCM audio if requested */
        if (cfg->audio && cfg->audio_pcm_path) {
            audio_file = fopen(cfg->audio_pcm_path, "rb");
            if (!audio_file) {
                fprintf(stderr, "error: cannot open audio PCM file '%s'\n",
                        cfg->audio_pcm_path);
            } else {
                int audio_sr = 44100;
                int audio_ch = 1;
                if (glif_writer_enable_audio(&gw, audio_sr, audio_ch,
                                             cfg->audio_rate,
                                             cfg->audio_depth) != 0) {
                    fprintf(stderr, "error: failed to enable audio encoder\n");
                    fclose(audio_file);
                    audio_file = NULL;
                } else {
                    /* Read one video frame worth of PCM per iteration */
                    audio_samples_per_frame = (cfg->fps > 0.0f)
                        ? audio_sr / (int)cfg->fps : audio_sr / 30;
                    audio_buf = malloc((size_t)audio_samples_per_frame *
                                       (size_t)audio_ch * sizeof(int16_t));
                    if (!audio_buf) {
                        fprintf(stderr, "error: failed to allocate audio buffer\n");
                        fclose(audio_file);
                        audio_file = NULL;
                    }
                    fprintf(stderr, "Audio: %d Hz → %d Hz %d-bit crushed PCM\n",
                            audio_sr, cfg->audio_rate, cfg->audio_depth);
                }
            }
        }
    }
    int needs_ppm_pipe = cfg->pipe_ppm || cfg->pipe_raw || cfg->v4l2_device;
    if (needs_ppm_pipe) {
        if (glif_ppm_pipe_init(&pp, &grid, &db, cfg->scale, cfg->dark_mode) != 0) {
            fprintf(stderr, "error: failed to allocate PPM pipe buffer\n");
            free(lm.data);
            glif_grid_free(&grid);
            free(frame_buf);
            glif_char_db_free(&db);
            glif_sampling_precompute_free(&pm);
            return 1;
        }
    } else if (cfg->color) {
        if (glif_frame_diff_init(&fd, rows, cols) != 0) {
            fprintf(stderr, "error: failed to allocate diff buffer\n");
            free(lm.data);
            glif_grid_free(&grid);
            free(frame_buf);
            glif_char_db_free(&db);
            glif_sampling_precompute_free(&pm);
            return 1;
        }
    }
#ifdef __linux__
    if (cfg->v4l2_device) {
        if (v4l2_output_init(&vo, cfg->v4l2_device,
                             (int)pp.img_w, (int)pp.img_h) != 0) {
            glif_ppm_pipe_free(&pp);
            free(lm.data);
            glif_grid_free(&grid);
            free(frame_buf);
            glif_char_db_free(&db);
            glif_sampling_precompute_free(&pm);
            return 1;
        }
    }
#endif

    fprintf(stderr, "Video: %dx%d @ %.0f fps, grid %dx%d (%d cells)\n",
            w, h, cfg->fps, cols, rows, cols * rows);
    if (cfg->pipe_raw) {
        fprintf(stderr, "Raw output: %zux%zu RGB24 (%dx%d grid, scale %d)\n",
                pp.img_w, pp.img_h, cols, rows, cfg->scale);
    }
    int terminal_output = !needs_ppm_pipe && !(cfg->glif_path && !cfg->color);
    if (terminal_output) {
        printf("\033[?25l\033[2J");
        fflush(stdout);
    }

    long frames = 0;
#ifndef __EMSCRIPTEN__
    double t_start = time_now();
    double t_pipeline_total = 0.0, t_render_total = 0.0;
#endif

    /* Read loop */
    while (fread(frame_buf, 1, frame_size, stdin) == frame_size) {
#ifndef __EMSCRIPTEN__
        double t_frame_start = time_now();
#endif

        /* Recompute lightness map in-place */
        if (glif_lightness_map_update(&lm, &img) != 0) break;
        if (cfg->adaptive.floor >= 0.0f)
            glif_lightness_map_normalize(&lm);

        /* Pipeline */
        glif_grid_compute_vectors_fast(&grid, &lm, &pm);
        glif_grid_compute_colors(&grid, &img);
        glif_contrast_analyze_frame(&cfg->adaptive, &grid);
        glif_contrast_directional(&grid, &sc, cfg->dir_crunch, &cfg->adaptive);
        glif_contrast_global(&grid, cfg->global_crunch, &cfg->adaptive);
        glif_match_grid(&grid, &db);

#ifndef __EMSCRIPTEN__
        double t_render_start = time_now();
        t_pipeline_total += t_render_start - t_frame_start;
#endif

        /* Render */
        if (gw.file) glif_writer_frame(&gw, &grid);

        /* Feed one video frame's worth of PCM to the crushed PCM encoder */
        if (audio_file && audio_buf) {
            size_t read = fread(audio_buf, sizeof(int16_t),
                                (size_t)audio_samples_per_frame, audio_file);
            if (read > 0)
                glif_writer_audio_samples(&gw, audio_buf, (int)read);
        }

        if (cfg->v4l2_device) {
            glif_ppm_pipe_render(&pp, &grid, &db);
#ifdef __linux__
            v4l2_output_frame(&vo, pp.pixels);
#endif
        } else if (cfg->pipe_raw) {
            glif_raw_pipe_frame(&pp, &grid, &db);
        } else if (cfg->pipe_ppm) {
            glif_ppm_pipe_frame(&pp, &grid, &db);
        } else if (cfg->color) {
            glif_frame_diff_render(&fd, &grid, cfg->dark_mode);
        } else if (!cfg->glif_path) {
            printf("\033[H");
            glif_output_plain(&grid);
            fflush(stdout);
        }

#ifndef __EMSCRIPTEN__
        t_render_total += time_now() - t_render_start;
#endif
        frames++;

#ifndef __EMSCRIPTEN__
        /* Frame pacing */
        if (frame_interval > 0.0) {
            double elapsed = time_now() - t_frame_start;
            double sleep_s = frame_interval - elapsed;
            if (sleep_s > 0.0) {
                struct timespec ts_sleep;
                ts_sleep.tv_sec = (time_t)sleep_s;
                ts_sleep.tv_nsec = (long)((sleep_s - (double)ts_sleep.tv_sec) * 1e9);
                nanosleep(&ts_sleep, NULL);
            }
        }
#endif
    }

#ifndef __EMSCRIPTEN__
    double t_total = time_now() - t_start;
#endif

    /* Show cursor, reset colors */
    if (terminal_output) {
        printf("\033[?25h\033[0m");
        fflush(stdout);
    }

#ifndef __EMSCRIPTEN__
    if (frames > 0) {
        fprintf(stderr, "Played %ld frames in %.1fs (%.1f fps avg)\n",
                frames, t_total, (double)frames / t_total);
        fprintf(stderr, "  pipeline: %.2f ms/frame, render: %.2f ms/frame\n",
                t_pipeline_total / (double)frames * 1e3,
                t_render_total / (double)frames * 1e3);
    }
#endif

    /* Cleanup */
    if (audio_file) fclose(audio_file);
    free(audio_buf);
    if (gw.file) {
        /* Embed original audio file if requested */
        if (cfg->keep_audio_path) {
            FILE *orig_f = fopen(cfg->keep_audio_path, "rb");
            if (!orig_f) {
                fprintf(stderr, "error: cannot open original audio '%s'\n",
                        cfg->keep_audio_path);
            } else {
                fseek(orig_f, 0, SEEK_END);
                long orig_len = ftell(orig_f);
                fseek(orig_f, 0, SEEK_SET);
                if (orig_len > 0) {
                    uint8_t *orig_data = malloc((size_t)orig_len);
                    if (orig_data) {
                        if (fread(orig_data, 1, (size_t)orig_len, orig_f) == (size_t)orig_len) {
                            glif_writer_keep_original_audio(&gw, orig_data, (size_t)orig_len);
                            fprintf(stderr, "Original audio: %s (%ld bytes)\n",
                                    cfg->keep_audio_path, orig_len);
                        }
                        free(orig_data);
                    }
                }
                fclose(orig_f);
            }
        }

        /* Capture audio stats before finish() frees the blip encoder */
        uint32_t audio_sample_count = 0;
        if (gw.blip) audio_sample_count = blip_encoder_samples(gw.blip);
        glif_writer_finish(&gw);
        fprintf(stderr, "Wrote %s (%u frames, %d×%d grid, %.1f fps",
                cfg->glif_path, gw.frames, cols, rows, cfg->fps);
        if (audio_sample_count > 0)
            fprintf(stderr, ", %u audio samples @ %d Hz %d-bit",
                    audio_sample_count, cfg->audio_rate, cfg->audio_depth);
        fprintf(stderr, ")\n");
    }
#ifdef __linux__
    if (cfg->v4l2_device) v4l2_output_free(&vo);
#endif
    glif_ppm_pipe_free(&pp);
    glif_frame_diff_free(&fd);
    free(lm.data);
    lm.data = NULL;
    glif_grid_free(&grid);
    free(frame_buf);
    glif_char_db_free(&db);
    glif_sampling_precompute_free(&pm);
    return 0;
}

int main(int argc, char **argv) {
    Config cfg;
    if (parse_args(&cfg, argc, argv) != 0) {
        usage(argv[0]);
        return 1;
    }

#if defined(__OpenBSD__) || defined(__linux__)
    #include "sandbox.h"
    /* Unveil only the paths this invocation needs */
    if (cfg.font_path)
        glif_unveil(cfg.font_path, "r");
    if (cfg.video) {
        if (cfg.glif_path)
            glif_unveil(cfg.glif_path, "wc");
        if (cfg.audio_pcm_path)
            glif_unveil(cfg.audio_pcm_path, "r");
        if (cfg.keep_audio_path)
            glif_unveil(cfg.keep_audio_path, "r");
        if (cfg.v4l2_device)
            glif_unveil(cfg.v4l2_device, "rw");
    } else {
        if (cfg.input_path)
            glif_unveil(cfg.input_path, "r");
        if (cfg.output_path)
            glif_unveil(cfg.output_path, "wc");
    }
    glif_unveil_lock();
    if (glif_pledge("stdio rpath wpath cpath tty") != 0)
        fprintf(stderr, "warning: pledge() failed\n");
#endif

    if (cfg.video) {
        return run_video(&cfg);
    } else {
        return run_image(&cfg);
    }
}
