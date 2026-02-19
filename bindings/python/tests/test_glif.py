"""Tests for the glif Python binding."""

import os
import pytest
import glif

# Test image shipped with the project
TEST_IMAGE = os.path.join(os.path.dirname(__file__), "..", "..", "..", "images", "raccoon.jpg")
TEST_FONT = os.environ.get("GLIF_TEST_FONT", "")


def requires_font(fn):
    return pytest.mark.skipif(
        not TEST_FONT or not os.path.isfile(TEST_FONT),
        reason="GLIF_TEST_FONT not set or font file not found",
    )(fn)


def requires_image(fn):
    return pytest.mark.skipif(
        not os.path.isfile(TEST_IMAGE),
        reason=f"Test image not found: {TEST_IMAGE}",
    )(fn)


@requires_font
@requires_image
def test_render_basic():
    result = glif.render(TEST_IMAGE, TEST_FONT)
    assert result.rows > 0
    assert result.cols > 0
    assert len(result.cells) == result.rows * result.cols


@requires_font
@requires_image
def test_render_characters_printable():
    result = glif.render(TEST_IMAGE, TEST_FONT)
    for cell in result.cells:
        assert 32 <= ord(cell.ch) <= 126, f"Non-printable char: {ord(cell.ch)}"


@requires_font
@requires_image
def test_render_plain_text_dimensions():
    result = glif.render(TEST_IMAGE, TEST_FONT)
    text = result.to_plain_text()
    lines = text.split("\n")
    assert len(lines) == result.rows
    for line in lines:
        assert len(line) == result.cols


@requires_font
@requires_image
def test_render_ansi_output():
    result = glif.render(TEST_IMAGE, TEST_FONT)
    ansi = result.to_ansi()
    assert "\033[38;2;" in ansi
    assert "\033[0m" in ansi


@requires_font
@requires_image
def test_render_custom_cell_size():
    result = glif.render(TEST_IMAGE, TEST_FONT, cell_width=8, cell_height=16)
    assert result.rows > 0
    assert result.cols > 0


@requires_font
@requires_image
def test_render_colors_populated():
    result = glif.render(TEST_IMAGE, TEST_FONT)
    has_nonzero = any(c.r > 0 or c.g > 0 or c.b > 0 for c in result.cells)
    assert has_nonzero, "All cells have zero color — something is wrong"


def test_error_invalid_image():
    with pytest.raises(RuntimeError, match="Failed to load image"):
        glif.render("/nonexistent/image.png", "/nonexistent/font.ttf")


@requires_font
def test_error_invalid_image_with_valid_font():
    with pytest.raises(RuntimeError, match="Failed to load image"):
        glif.render("/nonexistent/image.png", TEST_FONT)


@requires_image
def test_error_invalid_font():
    with pytest.raises(RuntimeError, match="Failed to load font"):
        glif.render(TEST_IMAGE, "/nonexistent/font.ttf")


# ── Low-level API tests ──


@requires_image
def test_image_context_manager():
    with glif.Image(TEST_IMAGE) as img:
        assert img.width > 0
        assert img.height > 0
        assert img.channels in (3, 4)


@requires_image
def test_lightness_map_context_manager():
    with glif.Image(TEST_IMAGE) as img:
        with glif.LightnessMap(img) as lm:
            assert lm.width == img.width
            assert lm.height == img.height


@requires_font
@requires_image
def test_low_level_pipeline():
    """Run the full pipeline through the low-level API."""
    with glif.Image(TEST_IMAGE) as img:
        with glif.LightnessMap(img) as lm:
            sc = glif.SamplingConfig()
            with glif.PrecomputedMasks(sc, 10, 20, lm.width) as pm:
                with glif.CharDatabase(TEST_FONT, 10, 20, sc) as db:
                    with glif.Grid(img, 10, 20) as grid:
                        grid.compute_vectors_fast(lm, pm)
                        grid.compute_colors(img)
                        grid.apply_directional_contrast(sc, 1.25)
                        grid.apply_global_contrast(1.5)
                        grid.match(db)
                        cells = grid.cells()
                        assert len(cells) == grid.rows * grid.cols
                        assert all(32 <= ord(c.ch) <= 126 for c in cells)


def test_image_error():
    with pytest.raises(RuntimeError):
        glif.Image("/nonexistent.png")
