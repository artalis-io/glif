#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
#include "output.h"

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
} Config;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <input.png> [options]\n"
        "\n"
        "Options:\n"
        "  -f, --font <path>        Monospace TTF (required)\n"
        "  -w, --cell-width <px>    Cell width (default: 10)\n"
        "  -h, --cell-height <px>   Cell height (default: 20)\n"
        "  -d, --dir-crunch <f>     Directional contrast crunch (default: 1.25)\n"
        "  -g, --global-crunch <f>  Global contrast crunch (default: 1.5)\n"
        "  -c, --color              ANSI truecolor terminal output\n"
        "  -o, --output <file.ppm>  Write PPM image file\n"
        "  -a, --auto-fit           Fit output to terminal size\n"
        "  -s, --scale <n>          PPM render scale (default: 4)\n"
        "  --help                   Show this message\n",
        prog);
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

    if (!cfg->input_path) {
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
    return 0;
}

int main(int argc, char **argv) {
    Config cfg;
    if (parse_args(&cfg, argc, argv) != 0) {
        usage(argv[0]);
        return 1;
    }

    /* 1. Load image */
    Image img;
    if (image_load(&img, cfg.input_path) != 0)
        return 1;

    /* 1b. Auto-fit cell size to terminal */
    if (cfg.auto_fit) {
        struct winsize ws;
        if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_col > 0 && ws.ws_row > 0) {
            int term_cols = ws.ws_col;
            int term_rows = ws.ws_row;
            cfg.cell_w = img.width / term_cols;
            cfg.cell_h = img.height / term_rows;
            if (cfg.cell_w < 2) cfg.cell_w = 2;
            if (cfg.cell_h < 2) cfg.cell_h = 2;
            fprintf(stderr, "Auto-fit: %dx%d terminal, cell size %dx%d\n",
                    term_cols, term_rows, cfg.cell_w, cfg.cell_h);
        } else {
            fprintf(stderr, "warning: cannot detect terminal size, using defaults\n");
        }
    }

    /* 2. Compute lightness map */
    LightnessMap lm;
    if (lightness_map_create(&lm, &img) != 0) {
        fprintf(stderr, "error: failed to create lightness map\n");
        image_free(&img);
        return 1;
    }

    /* 3. Init sampling geometry */
    SamplingConfig sc;
    sampling_config_init(&sc);

    /* 3b. Precompute circle masks for fast vector computation */
    PrecomputedMasks pm;
    if (sampling_precompute(&pm, &sc, cfg.cell_w, cfg.cell_h, lm.width) != 0) {
        fprintf(stderr, "error: failed to precompute sampling masks\n");
        lightness_map_free(&lm);
        image_free(&img);
        return 1;
    }

    /* 4. Load font + precompute character shape vectors */
    CharDatabase db;
    if (char_db_create(&db, cfg.font_path, cfg.cell_w, cfg.cell_h, &sc) != 0) {
        sampling_precompute_free(&pm);
        lightness_map_free(&lm);
        image_free(&img);
        return 1;
    }

    /* 5. Create grid */
    Grid grid;
    if (grid_create(&grid, &img, cfg.cell_w, cfg.cell_h) != 0) {
        fprintf(stderr, "error: image too small for cell size %dx%d\n",
                cfg.cell_w, cfg.cell_h);
        char_db_free(&db);
        sampling_precompute_free(&pm);
        lightness_map_free(&lm);
        image_free(&img);
        return 1;
    }

    /* 6. Compute per-cell vectors (fast precomputed path) */
    grid_compute_vectors_fast(&grid, &lm, &pm);

    /* 7. Compute per-cell average colors */
    grid_compute_colors(&grid, &img);

    /* 8. Directional contrast enhancement */
    contrast_directional(&grid, &sc, cfg.dir_crunch);

    /* 9. Global contrast enhancement */
    contrast_global(&grid, cfg.global_crunch);

    /* 10. Match cells to ASCII characters */
    match_grid(&grid, &db);

    /* 11. Output */
    if (cfg.output_path) {
        if (output_ppm(&grid, &db, cfg.output_path, cfg.scale) != 0) {
            fprintf(stderr, "error: failed to write PPM file\n");
        } else {
            fprintf(stderr, "Wrote %s (%zux%zu)\n", cfg.output_path,
                    (size_t)grid.cols * (size_t)cfg.cell_w * (size_t)cfg.scale,
                    (size_t)grid.rows * (size_t)cfg.cell_h * (size_t)cfg.scale);
        }
    } else if (cfg.color) {
        output_ansi(&grid);
    } else {
        output_plain(&grid);
    }

    /* Cleanup */
    grid_free(&grid);
    char_db_free(&db);
    sampling_precompute_free(&pm);
    lightness_map_free(&lm);
    image_free(&img);

    return 0;
}
