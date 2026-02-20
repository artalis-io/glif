CC      = cc
UNAME_S := $(shell uname -s)

# OpenMP: macOS uses libomp via brew; Linux uses libgomp via -fopenmp
ifeq ($(UNAME_S),Darwin)
  OMP_PREFIX = $(shell brew --prefix libomp 2>/dev/null)
  OMP_CFLAGS = -Xpreprocessor -fopenmp -I$(OMP_PREFIX)/include
  OMP_LDFLAGS = -L$(OMP_PREFIX)/lib -lomp
else
  OMP_CFLAGS = -fopenmp
  OMP_LDFLAGS = -fopenmp
endif

CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
          -D_FORTIFY_SOURCE=2 -fstack-protector-strong -O2 -Ivendor -Isrc \
          $(OMP_CFLAGS)
TCFLAGS = $(CFLAGS) -Wno-extra-semi
LDFLAGS = -lm $(OMP_LDFLAGS)

# Linux needs _DEFAULT_SOURCE for clock_gettime/nanosleep with -std=c11
# macOS exposes POSIX symbols by default; adding _POSIX_C_SOURCE restricts them
ifeq ($(UNAME_S),Linux)
  CFLAGS += -D_DEFAULT_SOURCE
endif

# Debug build with sanitizers (no OpenMP — conflicts with ASan)
DEBUG_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
               -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
               -Ivendor -Isrc
ifeq ($(UNAME_S),Linux)
  DEBUG_CFLAGS += -D_DEFAULT_SOURCE
endif
DEBUG_LDFLAGS = -lm -fsanitize=address,undefined

SRC = src/main.c src/image.c src/sampling.c src/grid.c src/font.c \
      src/contrast.c src/match.c src/output.c src/temporal.c src/compress.c \
      src/glif.c
OBJ = $(SRC:.c=.o)
BIN = glif

# Library objects (everything except main.o)
LIB_OBJ = src/image.o src/sampling.o src/grid.o src/font.o \
           src/contrast.o src/match.o src/output.o src/temporal.o src/compress.o \
           src/glif.o

# Linux-only: v4l2 output
UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
  SRC += src/platform/linux/v4l2_output.c
  LIB_OBJ += src/platform/linux/v4l2_output.o
endif

TESTS = tests/test_vec6 tests/test_sampling tests/test_image \
        tests/test_grid tests/test_contrast tests/test_match \
        tests/test_font tests/test_output tests/test_temporal \
        tests/test_compress tests/test_glif

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/platform/linux/%.o: src/platform/linux/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/platform/wasm/%.o: src/platform/wasm/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test targets — each links against the library objects it needs
tests/test_vec6: tests/test_vec6.c src/vec6.h
	$(CC) $(TCFLAGS) -o $@ tests/test_vec6.c $(LDFLAGS)

tests/test_sampling: tests/test_sampling.c src/sampling.o
	$(CC) $(TCFLAGS) -o $@ tests/test_sampling.c src/sampling.o $(LDFLAGS)

tests/test_image: tests/test_image.c src/image.o
	$(CC) $(TCFLAGS) -o $@ tests/test_image.c src/image.o $(LDFLAGS)

tests/test_grid: tests/test_grid.c src/image.o src/sampling.o src/grid.o
	$(CC) $(TCFLAGS) -o $@ tests/test_grid.c src/image.o src/sampling.o src/grid.o $(LDFLAGS)

tests/test_contrast: tests/test_contrast.c src/contrast.o src/sampling.o
	$(CC) $(TCFLAGS) -o $@ tests/test_contrast.c src/contrast.o src/sampling.o $(LDFLAGS)

tests/test_match: tests/test_match.c src/sampling.o src/match.o src/font.o src/image.o
	$(CC) $(TCFLAGS) -o $@ tests/test_match.c src/sampling.o src/match.o src/font.o src/image.o $(LDFLAGS)

tests/test_font: tests/test_font.c src/font.o src/sampling.o src/image.o
	$(CC) $(TCFLAGS) -o $@ tests/test_font.c src/font.o src/sampling.o src/image.o $(LDFLAGS)

tests/test_output: tests/test_output.c src/output.o src/compress.o src/font.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o
	$(CC) $(TCFLAGS) -o $@ tests/test_output.c src/output.o src/compress.o src/font.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o $(LDFLAGS)

tests/test_temporal: tests/test_temporal.c src/temporal.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o src/font.o
	$(CC) $(TCFLAGS) -o $@ tests/test_temporal.c src/temporal.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o src/font.o $(LDFLAGS)

tests/test_compress: tests/test_compress.c src/compress.o
	$(CC) $(TCFLAGS) -o $@ tests/test_compress.c src/compress.o $(LDFLAGS)

tests/test_glif: tests/test_glif.c src/glif.o src/compress.o src/output.o src/grid.o src/image.o src/sampling.o src/font.o src/contrast.o src/match.o
	$(CC) $(TCFLAGS) -o $@ tests/test_glif.c src/glif.o src/compress.o src/output.o src/grid.o src/image.o src/sampling.o src/font.o src/contrast.o src/match.o $(LDFLAGS)

test: $(LIB_OBJ) $(TESTS)
	@echo "=== Running tests ==="
	@fail=0; \
	for t in $(TESTS); do \
		echo "--- $$t ---"; \
		./$$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "=== All tests passed ==="; \
	else echo "=== Some tests failed ==="; exit 1; fi

tools/bench: tools/bench.c $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ tools/bench.c $(LIB_OBJ) $(LDFLAGS)

debug: clean
	$(CC) $(DEBUG_CFLAGS) -o $(BIN) $(SRC) $(DEBUG_LDFLAGS)

# Core library (shared between native + WASM)
CORE_SRC = src/image.c src/sampling.c src/grid.c src/font.c \
           src/contrast.c src/match.c src/temporal.c

# WASM build via Emscripten (Nuklear + Clay UI)
WASM_UI_SRC = $(CORE_SRC) \
              src/platform/wasm/ui.c \
              src/platform/wasm/nk_webgl.c \
              src/platform/wasm/nk_impl.c \
              src/platform/wasm/clay_impl.c \
              src/platform/wasm/ui_layout.c
WASM_UI_OUT = web/glif.js

WASM_EXPORTS = '_app_init','_app_resize','_app_set_dpr','_app_frame','_app_mouse','_app_key', \
               '_app_touch','_app_load_font','_app_load_image','_app_video_frame', \
               '_app_switch_camera','_app_get_camera_count','_malloc','_free'

wasm: $(WASM_UI_SRC)
	@mkdir -p web
	emcc -std=gnu11 -O2 -msimd128 -Ivendor -Isrc \
	  -Isrc/platform/wasm \
	  -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s FULL_ES2=1 \
	  -s MODULARIZE=1 -s EXPORT_NAME='createGlifModule' \
	  -s "EXPORTED_FUNCTIONS=[$(WASM_EXPORTS)]" \
	  -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAP8']" \
	  -s NO_FILESYSTEM=1 --no-entry \
	  -o $(WASM_UI_OUT) $(WASM_UI_SRC) -lm

# WASM build for Chrome extension (no UI framework, just pipeline + WebGL)
WASM_EXT_SRC = $(CORE_SRC) src/platform/wasm/ext.c

WASM_EXT_EXPORTS = '_ext_init','_ext_resize','_ext_frame','_ext_render', \
                   '_ext_set_params','_ext_set_hires','_malloc','_free'

# Shared library
ifeq ($(UNAME_S),Darwin)
  SHARED_LIB = libglif.dylib
  SHARED_FLAGS = -dynamiclib -install_name @rpath/$(SHARED_LIB)
else
  SHARED_LIB = libglif.so
  SHARED_FLAGS = -shared
endif

LIB_PIC_OBJ = $(LIB_OBJ:.o=.pic.o)

src/%.pic.o: src/%.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

src/platform/linux/%.pic.o: src/platform/linux/%.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

$(SHARED_LIB): $(LIB_PIC_OBJ)
	$(CC) $(SHARED_FLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: shared
shared: $(SHARED_LIB)

wasm-ext: $(WASM_EXT_SRC)
	@mkdir -p extension/wasm
	emcc -std=gnu11 -O2 -msimd128 -Ivendor -Isrc \
	  -Isrc/platform/wasm \
	  -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s FULL_ES2=1 \
	  -s MODULARIZE=1 -s EXPORT_NAME='createGlifExt' \
	  -s "EXPORTED_FUNCTIONS=[$(WASM_EXT_EXPORTS)]" \
	  -s "EXPORTED_RUNTIME_METHODS=['HEAPU8']" \
	  -s NO_FILESYSTEM=1 --no-entry \
	  -o extension/wasm/glif-ext.js $(WASM_EXT_SRC) -lm

# WASM player for .glif playback (minimal deps, no pipeline)
WASM_PLAYER_SRC = src/glif.c src/compress.c src/platform/wasm/player.c

WASM_PLAYER_EXPORTS = '_player_init','_player_load','_player_decode_frame', \
                      '_player_render','_player_resize','_player_free', \
                      '_player_get_frames','_player_get_fps', \
                      '_player_get_cols','_player_get_rows', \
                      '_player_get_cell_w','_player_get_cell_h', \
                      '_player_get_flags','_malloc','_free'

wasm-player: $(WASM_PLAYER_SRC)
	@mkdir -p web
	emcc -std=gnu11 -O2 -Ivendor -Isrc \
	  -Isrc/platform/wasm \
	  -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s FULL_ES2=1 \
	  -s MODULARIZE=1 -s EXPORT_NAME='createGlifPlayer' \
	  -s "EXPORTED_FUNCTIONS=[$(WASM_PLAYER_EXPORTS)]" \
	  -s "EXPORTED_RUNTIME_METHODS=['HEAPU8']" \
	  -s NO_FILESYSTEM=1 --no-entry \
	  -o web/glif-player-wasm.js $(WASM_PLAYER_SRC) -lm

clean:
	rm -f $(OBJ) $(BIN) $(TESTS)
	rm -f src/*.pic.o src/platform/linux/*.o src/platform/linux/*.pic.o src/platform/wasm/*.o
	rm -f libglif.so libglif.dylib

.PHONY: all clean test debug wasm wasm-ext wasm-player shared
