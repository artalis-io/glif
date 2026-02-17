CC      = cc
OMP_PREFIX = $(shell brew --prefix libomp 2>/dev/null)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
          -fstack-protector-strong -O2 -Ivendor -Isrc \
          -Xpreprocessor -fopenmp -I$(OMP_PREFIX)/include
TCFLAGS = $(CFLAGS) -Wno-extra-semi
LDFLAGS = -lm -L$(OMP_PREFIX)/lib -lomp

# Debug build with sanitizers (no OpenMP — conflicts with ASan)
DEBUG_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
               -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
               -Ivendor -Isrc
DEBUG_LDFLAGS = -lm -fsanitize=address,undefined

SRC = src/main.c src/image.c src/sampling.c src/grid.c src/font.c \
      src/contrast.c src/match.c src/output.c src/temporal.c
OBJ = $(SRC:.c=.o)
BIN = glif

# Library objects (everything except main.o)
LIB_OBJ = src/image.o src/sampling.o src/grid.o src/font.o \
           src/contrast.o src/match.o src/output.o src/temporal.o

# Linux-only: v4l2 output
UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
  SRC += src/platform/linux/v4l2_output.c
  LIB_OBJ += src/platform/linux/v4l2_output.o
endif

TESTS = tests/test_vec6 tests/test_sampling tests/test_image \
        tests/test_grid tests/test_contrast tests/test_match \
        tests/test_font tests/test_output tests/test_temporal

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

tests/test_output: tests/test_output.c src/output.o src/font.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o
	$(CC) $(TCFLAGS) -o $@ tests/test_output.c src/output.o src/font.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o $(LDFLAGS)

tests/test_temporal: tests/test_temporal.c src/temporal.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o src/font.o
	$(CC) $(TCFLAGS) -o $@ tests/test_temporal.c src/temporal.o src/image.o src/sampling.o src/grid.o src/contrast.o src/match.o src/font.o $(LDFLAGS)

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

# Legacy WASM build (old JS-based UI)
WASM_LEGACY_SRC = $(CORE_SRC) src/platform/wasm/wasm_api.c

wasm-legacy: $(WASM_LEGACY_SRC)
	@mkdir -p web
	emcc -std=c11 -O2 -msimd128 -Ivendor -Isrc \
	  -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 \
	  -s MODULARIZE=1 -s EXPORT_NAME='createGlifModule' \
	  -s "EXPORTED_FUNCTIONS=['_glif_init','_glif_process_frame','_glif_free','_malloc','_free']" \
	  -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAP8']" \
	  -s NO_FILESYSTEM=1 --no-entry \
	  -o web/glif.js $(WASM_LEGACY_SRC) -lm

# WASM build via Emscripten (Nuklear + Clay UI)
WASM_UI_SRC = $(CORE_SRC) \
              src/platform/wasm/ui.c \
              src/platform/wasm/nk_webgl.c \
              src/platform/wasm/nk_impl.c \
              src/platform/wasm/clay_impl.c \
              src/platform/wasm/ui_layout.c
WASM_UI_OUT = web/glif.js

WASM_EXPORTS = '_app_init','_app_resize','_app_frame','_app_mouse','_app_key', \
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

clean:
	rm -f $(OBJ) $(BIN) $(TESTS)
	rm -f src/platform/linux/*.o src/platform/wasm/*.o

.PHONY: all clean test debug wasm wasm-legacy
