"""Glif — shape-based ASCII art renderer.

High-level API:
    result = glif.render("photo.png", "font.ttf")
    print(result.to_plain_text())

Low-level API:
    with glif.Image("photo.png") as img:
        with glif.LightnessMap(img) as lm:
            ...
"""

from ._ffi import ffi, lib

__all__ = [
    "render",
    "RenderResult",
    "Cell",
    "Image",
    "LightnessMap",
    "SamplingConfig",
    "PrecomputedMasks",
    "CharDatabase",
    "Grid",
]


# ── High-level API ──────────────────────────────────────────────────────────


class Cell:
    """A single rendered cell: character + RGB color."""

    __slots__ = ("ch", "r", "g", "b")

    def __init__(self, ch, r, g, b):
        self.ch = ch
        self.r = r
        self.g = g
        self.b = b

    def __repr__(self):
        return f"Cell({self.ch!r}, {self.r}, {self.g}, {self.b})"


class RenderResult:
    """Result of render(): grid of cells with convenience output methods."""

    def __init__(self, rows, cols, cells):
        self.rows = rows
        self.cols = cols
        self.cells = cells

    def to_plain_text(self):
        """Return plain ASCII string (newline-separated rows)."""
        lines = []
        for r in range(self.rows):
            offset = r * self.cols
            lines.append("".join(self.cells[offset + c].ch for c in range(self.cols)))
        return "\n".join(lines)

    def to_ansi(self):
        """Return ANSI truecolor string."""
        parts = []
        for r in range(self.rows):
            offset = r * self.cols
            for c in range(self.cols):
                cell = self.cells[offset + c]
                parts.append(f"\033[38;2;{cell.r};{cell.g};{cell.b}m{cell.ch}")
            parts.append("\033[0m\n")
        return "".join(parts)


def render(image_path, font_path, cell_width=10, cell_height=20,
           dir_crunch=1.25, global_crunch=1.5):
    """Render an image to ASCII art.

    Args:
        image_path: Path to input image (PNG, JPEG, etc.)
        font_path: Path to monospace TTF font
        cell_width: Width of each grid cell in pixels (default 10)
        cell_height: Height of each grid cell in pixels (default 20)
        dir_crunch: Directional contrast exponent (default 1.25)
        global_crunch: Global contrast exponent (default 1.5)

    Returns:
        RenderResult with .rows, .cols, .cells, .to_plain_text(), .to_ansi()
    """
    # 1. Load image
    img = ffi.new("GlifImage *")
    if lib.glif_image_load(img, image_path.encode("utf-8")) != 0:
        raise RuntimeError(f"Failed to load image: {image_path}")

    try:
        # 2. Lightness map
        lm = ffi.new("GlifLightnessMap *")
        if lib.glif_lightness_map_create(lm, img) != 0:
            raise RuntimeError("Failed to create lightness map")

        try:
            # 3. Sampling geometry
            sc = ffi.new("GlifSamplingConfig *")
            lib.glif_sampling_config_init(sc)

            pm = ffi.new("GlifPrecomputedMasks *")
            if lib.glif_sampling_precompute(pm, sc, cell_width, cell_height, lm.width) != 0:
                raise RuntimeError("Failed to precompute sampling masks")

            try:
                # 4. Font database
                db = ffi.new("GlifCharDatabase *")
                if lib.glif_char_db_create(db, font_path.encode("utf-8"),
                                      cell_width, cell_height, sc) != 0:
                    raise RuntimeError(f"Failed to load font: {font_path}")

                try:
                    # 5. Grid
                    grid = ffi.new("GlifGrid *")
                    if lib.glif_grid_create(grid, img, cell_width, cell_height) != 0:
                        raise RuntimeError(
                            f"GlifImage too small for cell size {cell_width}x{cell_height}"
                        )

                    try:
                        # 6-10. Pipeline
                        lib.glif_grid_compute_vectors_fast(grid, lm, pm)
                        lib.glif_grid_compute_colors(grid, img)

                        ac = ffi.new("GlifAdaptiveContrast *")
                        ac.floor = -1.0  # disabled
                        lib.glif_contrast_analyze_frame(ac, grid)
                        lib.glif_contrast_directional(grid, sc, dir_crunch, ac)
                        lib.glif_contrast_global(grid, global_crunch, ac)
                        lib.glif_match_grid(grid, db)

                        # Extract cells
                        rows = grid.rows
                        cols = grid.cols
                        total = rows * cols
                        cells = []
                        for i in range(total):
                            c = grid.cells[i]
                            cells.append(Cell(
                                c.ch.decode("ascii"), c.r, c.g, c.b
                            ))

                        return RenderResult(rows, cols, cells)
                    finally:
                        lib.glif_grid_free(grid)
                finally:
                    lib.glif_char_db_free(db)
            finally:
                lib.glif_sampling_precompute_free(pm)
        finally:
            lib.glif_lightness_map_free(lm)
    finally:
        lib.glif_image_free(img)


# ── Low-level API ───────────────────────────────────────────────────────────


class Image:
    """Wrapper around C Image struct. Use as context manager."""

    def __init__(self, path):
        self._ptr = ffi.new("GlifImage *")
        self._freed = False
        if lib.glif_image_load(self._ptr, path.encode("utf-8")) != 0:
            raise RuntimeError(f"Failed to load image: {path}")

    @property
    def width(self):
        return self._ptr.width

    @property
    def height(self):
        return self._ptr.height

    @property
    def channels(self):
        return self._ptr.channels

    def free(self):
        if not self._freed:
            lib.glif_image_free(self._ptr)
            self._freed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.free()

    def __del__(self):
        self.free()


class LightnessMap:
    """Wrapper around C LightnessMap struct."""

    def __init__(self, image):
        self._ptr = ffi.new("GlifLightnessMap *")
        self._freed = False
        if lib.glif_lightness_map_create(self._ptr, image._ptr) != 0:
            raise RuntimeError("Failed to create lightness map")

    @property
    def width(self):
        return self._ptr.width

    @property
    def height(self):
        return self._ptr.height

    def normalize(self):
        lib.glif_lightness_map_normalize(self._ptr)

    def free(self):
        if not self._freed:
            lib.glif_lightness_map_free(self._ptr)
            self._freed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.free()

    def __del__(self):
        self.free()


class SamplingConfig:
    """Wrapper around C SamplingConfig struct."""

    def __init__(self):
        self._ptr = ffi.new("GlifSamplingConfig *")
        lib.glif_sampling_config_init(self._ptr)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        pass


class PrecomputedMasks:
    """Wrapper around C PrecomputedMasks struct."""

    def __init__(self, sampling_config, cell_w, cell_h, stride):
        self._ptr = ffi.new("GlifPrecomputedMasks *")
        self._freed = False
        if lib.glif_sampling_precompute(self._ptr, sampling_config._ptr,
                                   cell_w, cell_h, stride) != 0:
            raise RuntimeError("Failed to precompute sampling masks")

    def free(self):
        if not self._freed:
            lib.glif_sampling_precompute_free(self._ptr)
            self._freed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.free()

    def __del__(self):
        self.free()


class CharDatabase:
    """Wrapper around C CharDatabase struct."""

    def __init__(self, font_path, cell_w, cell_h, sampling_config):
        self._ptr = ffi.new("GlifCharDatabase *")
        self._freed = False
        if lib.glif_char_db_create(self._ptr, font_path.encode("utf-8"),
                              cell_w, cell_h, sampling_config._ptr) != 0:
            raise RuntimeError(f"Failed to load font: {font_path}")

    def free(self):
        if not self._freed:
            lib.glif_char_db_free(self._ptr)
            self._freed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.free()

    def __del__(self):
        self.free()


class Grid:
    """Wrapper around C Grid struct."""

    def __init__(self, image, cell_w, cell_h):
        self._ptr = ffi.new("GlifGrid *")
        self._freed = False
        if lib.glif_grid_create(self._ptr, image._ptr, cell_w, cell_h) != 0:
            raise RuntimeError(f"GlifImage too small for cell size {cell_w}x{cell_h}")

    @property
    def rows(self):
        return self._ptr.rows

    @property
    def cols(self):
        return self._ptr.cols

    def compute_vectors_fast(self, lightness_map, precomputed_masks):
        lib.glif_grid_compute_vectors_fast(self._ptr, lightness_map._ptr,
                                      precomputed_masks._ptr)

    def compute_colors(self, image):
        lib.glif_grid_compute_colors(self._ptr, image._ptr)

    def match(self, char_db):
        lib.glif_match_grid(self._ptr, char_db._ptr)

    def cells(self):
        """Return list of Cell objects from the grid."""
        total = self._ptr.rows * self._ptr.cols
        result = []
        for i in range(total):
            c = self._ptr.cells[i]
            result.append(Cell(c.ch.decode("ascii"), c.r, c.g, c.b))
        return result

    def apply_directional_contrast(self, sampling_config, crunch,
                                   adaptive_floor=-1.0):
        ac = ffi.new("GlifAdaptiveContrast *")
        ac.floor = adaptive_floor
        lib.glif_contrast_analyze_frame(ac, self._ptr)
        lib.glif_contrast_directional(self._ptr, sampling_config._ptr, crunch, ac)

    def apply_global_contrast(self, crunch, adaptive_floor=-1.0):
        ac = ffi.new("GlifAdaptiveContrast *")
        ac.floor = adaptive_floor
        lib.glif_contrast_analyze_frame(ac, self._ptr)
        lib.glif_contrast_global(self._ptr, crunch, ac)

    def free(self):
        if not self._freed:
            lib.glif_grid_free(self._ptr)
            self._freed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.free()

    def __del__(self):
        self.free()
