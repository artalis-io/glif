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
      src/contrast.c src/match.c src/output.c
OBJ = $(SRC:.c=.o)
BIN = ascii3d

# Library objects (everything except main.o)
LIB_OBJ = src/image.o src/sampling.o src/grid.o src/font.o \
           src/contrast.o src/match.o src/output.o

TESTS = tests/test_vec6 tests/test_sampling tests/test_image \
        tests/test_grid tests/test_contrast tests/test_match \
        tests/test_font tests/test_output

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
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

clean:
	rm -f $(OBJ) $(BIN) $(TESTS)

.PHONY: all clean test debug
