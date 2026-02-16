---
name: c-audit
description: Audit C code for security, safety, and memory management. Use when reviewing or hardening C modules.
user-invocable: true
---

# C Code Audit Skill

Perform comprehensive security, safety, and quality audits on ascii3d C code.

**Target:** $ARGUMENTS (default: all `src/` files)

## Usage

```
/c-audit                    # Audit all source files
/c-audit src/match.c        # Audit a specific file
/c-audit --fix              # Audit and apply fixes
```

## Audit Categories

### 1. Memory Safety (Critical)

| Issue | Pattern to Find | Severity |
|-------|-----------------|----------|
| Buffer overflow | `strcpy`, `strcat`, `sprintf`, `gets`, unbounded loops | Critical |
| Unbounded string ops | `strlen`, `strcmp` on untrusted input | Critical |
| Unsafe integer parsing | `atoi`, `atol`, `atof` (no error detection, no bounds) | High |
| Integer overflow | `malloc(a * b)` without overflow check | Critical |
| Use-after-free | Pointer used after `free()` | Critical |
| Double-free | `free()` called twice on same pointer | Critical |
| Null dereference | Pointer used without NULL check | High |
| Uninitialized memory | Variables used before assignment | High |
| Missing null terminator | String buffer not explicitly terminated | High |
| Memory leak | `malloc`/`calloc` without corresponding `free` | Medium |
| Stack buffer overflow | Large stack arrays, VLAs | Medium |

**Safe Replacements:**
```c
// Copying
strcpy(dst, src)           -> strncpy(dst, src, sizeof(dst)-1); dst[sizeof(dst)-1] = '\0';
strcat(dst, src)           -> strncat(dst, src, sizeof(dst)-strlen(dst)-1);

// Formatting
sprintf(buf, fmt, ...)     -> snprintf(buf, sizeof(buf), fmt, ...);
gets(buf)                  -> fgets(buf, sizeof(buf), stdin);

// Memory allocation (overflow-safe)
malloc(count * size)       -> calloc(count, size);

// Integer parsing (atoi/atol have no error detection!)
atoi(str)                  -> strtol(str, &end, 10) with validation
atof(str)                  -> strtof(str, &end) with validation
```

**Integer Parsing Pattern:**
```c
// BAD: atoi() returns 0 for "abc", no overflow detection
int width = atoi(value);

// GOOD: strtol with full validation
char *end;
long val = strtol(value, &end, 10);
if (end == value || *end != '\0' || val < min || val > max) {
    fprintf(stderr, "error: invalid value '%s'\n", value);
    return -1;
}
```

**Null Termination Rule:**
```c
// BAD: strncpy doesn't guarantee null termination
strncpy(buf, input, sizeof(buf));

// GOOD: explicit null termination
strncpy(buf, input, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';

// GOOD: snprintf always null-terminates
snprintf(buf, sizeof(buf), "%s", input);
```

### 2. Input Validation

| Issue | What to Check |
|-------|---------------|
| Array bounds | All array indices validated before access |
| Pointer validity | NULL checks before dereference |
| Size parameters | Non-negative, within reasonable bounds |
| String length | Length checked before copy/concat |
| Numeric ranges | Values within expected domain |

**Pattern:**
```c
if (index < 0 || (size_t)index >= count) {
    return -1;
}
data[index] = value;
```

### 3. Resource Management

| Issue | What to Check |
|-------|---------------|
| File handles | `fopen` paired with `fclose` |
| Memory | `malloc`/`calloc` paired with `free` |
| stb resources | `stbi_load` paired with `stbi_image_free` |
| Error paths | Resources freed on all exit paths |
| I/O return values | `fwrite`/`fread` return values checked |

**ascii3d Cleanup Pattern:**
```c
// Every _create()/_load() must have matching _free()
image_load()           -> image_free()
lightness_map_create() -> lightness_map_free()
grid_create()          -> grid_free()
char_db_create()       -> char_db_free()
match_cache_init()     -> match_cache_free()
sampling_precompute()  -> sampling_precompute_free()
```

### 4. Integer Overflow

Overflow in size computations can cause undersized allocations and buffer overflows.

```c
// BAD: overflow on 32-bit
int total = width * height * channels;
uint8_t *buf = malloc(total);

// GOOD: use size_t and calloc
size_t npixels = (size_t)width * (size_t)height;
uint8_t *buf = calloc(npixels, channels);

// GOOD: check before multiply
if (width > 0 && height > SIZE_MAX / (size_t)width) {
    return -1;  // overflow
}
```

**Key areas in ascii3d:**
- Image buffer allocations (`width * height * channels`)
- Grid cell arrays (`rows * cols * sizeof(GridCell)`)
- PPM output buffers (`img_w * img_h * 3`)
- Font bitmap allocations (`cell_w * cell_h`)
- Render bitmap allocations (`cell_w * scale * cell_h * scale`)

### 5. OpenMP Thread Safety

ascii3d uses OpenMP for parallelism. Check for data races.

| Issue | Severity | Pattern |
|-------|----------|---------|
| Shared mutable state in parallel region | Critical | Writing to shared variable without atomic/critical |
| Race on cache/lookup table | High | Multiple threads writing same cache entry |
| Non-thread-safe function in parallel region | High | `strtok`, `rand`, non-reentrant functions |
| False sharing | Low | Adjacent cache lines written by different threads |

**Per-Thread State Pattern (used in match.c):**
```c
#pragma omp parallel
{
    // Per-thread resource — no data race
    MatchCache mc;
    match_cache_init(&mc);

    #pragma omp for schedule(static)
    for (int i = 0; i < n; i++) {
        // Each iteration writes to distinct cell — safe
        cells[i].ch = match_find(&cells[i].shape, db, &mc);
    }

    match_cache_free(&mc);
}
```

**Safe Parallel Patterns:**
- `#pragma omp parallel for` on loops where each iteration writes to a distinct index
- `#pragma omp simd reduction(+:sum)` for accumulation
- Per-thread scratch buffers allocated inside `#pragma omp parallel` block
- Read-only shared data (image, lightness map, char DB) is safe

**Unsafe Patterns:**
- Multiple threads writing to the same global/static variable
- Shared cache without per-thread copies
- `srgb_lut_init()` called from parallel region (init once before parallel)

### 6. Defensive Macros

Check for and suggest these patterns:
```c
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define SAFE_FREE(p) do { free(p); (p) = NULL; } while(0)
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
```

### 7. Test Coverage

Check test files (`tests/test_*.c`) for:
- [ ] Basic functionality tests
- [ ] Edge cases (empty input, max values, NULL)
- [ ] Error path tests (what happens when things fail)
- [ ] Bounds checking tests
- [ ] All public API functions have at least one test

### 8. Dead Code Detection

| Pattern | Issue | Fix |
|---------|-------|-----|
| `if (0) { ... }` | Dead branch | Remove |
| `return; code_after;` | Unreachable code | Remove |
| `#if 0 ... #endif` | Disabled code | Remove or document |
| Unused `#define` | Dead macro | Remove |
| Unused static function | Dead function | Remove |

Compile with `-Wunused` flags to detect automatically.

### 9. Build Hardening

**Development build (`make debug`):**
```makefile
-fsanitize=address,undefined -g -fno-omit-frame-pointer
```

**Production build (`make`):**
```makefile
-Wall -Wextra -Wpedantic -Wshadow -Wformat=2
-fstack-protector-strong
-O2
```

**Audit Checks:**
- [ ] `-fstack-protector-strong` in production CFLAGS
- [ ] Debug build with ASan + UBSan available (`make debug`)
- [ ] All tests pass under sanitizers
- [ ] No compiler warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2`

## Audit Procedure

When `/c-audit` is invoked:

1. **Locate Files**
   ```
   src/*.c src/*.h    # Source files
   tests/test_*.c     # Test files
   tools/*.c          # Tool source
   Makefile           # Build configuration
   ```

2. **Scan for Critical Issues**
   - Search for unsafe functions: `strcpy`, `sprintf`, `gets`, `strcat`
   - Search for unsafe integer parsing: `atoi`, `atol`, `atof`
   - Search for unchecked allocations: `malloc`/`calloc` without NULL check
   - Search for integer overflow in size calculations
   - Search for missing bounds checks on array access

3. **Review Public API**
   - Check all public functions in headers
   - Verify NULL checks on pointer parameters
   - Verify bounds checks on size parameters

4. **Check Resource Management**
   - Every `_create()`/`_load()` has matching `_free()`
   - Error paths free allocated resources
   - No memory leaks on early returns
   - `fwrite`/`fread` return values checked

5. **Check OpenMP Thread Safety**
   - No shared mutable state in parallel regions (except per-thread copies)
   - Global init (sRGB LUT) happens before any parallel region
   - Per-thread caches used for match operations
   - Each loop iteration writes only to its own cell/index

6. **Detect Dead Code**
   - Compile with `-Wunused` flags
   - Find unused static functions
   - Find unused variables and parameters
   - Flag commented-out or `#if 0` code blocks

7. **Check Build Hardening**
   - Sanitizers available in debug build
   - Stack protection in production build
   - Warning flags comprehensive

8. **Generate Report**
   Format as markdown table with findings, severity, file:line, and suggested fix.

## Report Format

```markdown
## C Audit Report: ascii3d

**Date:** YYYY-MM-DD
**Files Scanned:** N
**Issues Found:** N (Critical: N, High: N, Medium: N, Low: N)

### Critical Issues

| # | File:Line | Issue | Current Code | Suggested Fix |
|---|-----------|-------|--------------|---------------|
| C1 | src/foo.c:42 | Buffer overflow | `strcpy(buf, src)` | `snprintf(buf, sizeof(buf), "%s", src)` |

### High Issues
...

### Medium Issues
...

### Low Issues
...

### Recommendations
1. ...
```

## Fix Mode (--fix)

When `--fix` is specified:

1. Generate the audit report first
2. For each fixable issue, apply the transformation
3. Rebuild (`make`)
4. Re-run tests (`make test`)
5. Report any test failures or new warnings

**Auto-fixable Issues:**
- `strcpy` -> `snprintf` with buffer size
- `sprintf` -> `snprintf` with buffer size
- `atoi` -> `strtol` with validation
- `atof` -> `strtof` with validation
- Missing NULL checks (add early return)
- Missing `calloc` return check (add NULL check)
- Integer overflow in `malloc(a * b)` -> `calloc(a, b)`
- Missing `size_t` casts in size calculations
- Missing `fwrite` return value checks
- Unused local variables (remove)
- Unused static functions (remove)

**NOT Auto-fixable (require manual review):**
- Logic errors
- Resource leaks in complex control flow
- OpenMP thread safety issues
- Architectural changes
