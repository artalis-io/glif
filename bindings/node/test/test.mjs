import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const glif = require('../lib/index.js');

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(__dirname, '..', '..', '..');
const TEST_IMAGE = path.join(projectRoot, 'images', 'raccoon.jpg');
const TEST_FONT = process.env.GLIF_TEST_FONT || '';

describe('render (high-level)', () => {
    it('renders with correct dimensions', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT);
        assert.ok(result.rows > 0);
        assert.ok(result.cols > 0);
        assert.equal(result.cells.length, result.rows * result.cols);
    });

    it('produces printable ASCII characters', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT);
        for (const cell of result.cells) {
            const code = cell.ch.charCodeAt(0);
            assert.ok(code >= 32 && code <= 126,
                `Non-printable char: ${code}`);
        }
    });

    it('toPlainText has correct dimensions', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT);
        const lines = result.toPlainText().split('\n');
        assert.equal(lines.length, result.rows);
        for (const line of lines) {
            assert.equal(line.length, result.cols);
        }
    });

    it('toAnsi contains escape codes', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT);
        const ansi = result.toAnsi();
        assert.ok(ansi.includes('\x1b[38;2;'));
        assert.ok(ansi.includes('\x1b[0m'));
    });

    it('supports custom cell size', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT,
            { cellWidth: 8, cellHeight: 16 });
        assert.ok(result.rows > 0);
        assert.ok(result.cols > 0);
    });

    it('has non-zero colors', () => {
        if (!TEST_FONT) return;
        const result = glif.render(TEST_IMAGE, TEST_FONT);
        const hasColor = result.cells.some(c => c.r > 0 || c.g > 0 || c.b > 0);
        assert.ok(hasColor, 'All cells have zero color');
    });

    it('throws on invalid image', () => {
        assert.throws(
            () => glif.render('/nonexistent/image.png', '/nonexistent/font.ttf'),
            /Failed to load image/
        );
    });

    it('throws on invalid font', () => {
        assert.throws(
            () => glif.render(TEST_IMAGE, '/nonexistent/font.ttf'),
            /Failed to load font/
        );
    });
});

describe('low-level API', () => {
    it('Image loads and has properties', () => {
        const img = new glif.Image(TEST_IMAGE);
        assert.ok(img.width > 0);
        assert.ok(img.height > 0);
        assert.ok(img.channels === 3 || img.channels === 4);
        img.free();
    });

    it('Image throws on invalid path', () => {
        assert.throws(
            () => new glif.Image('/nonexistent.png'),
            /Failed to load image/
        );
    });

    it('LightnessMap has correct dimensions', () => {
        const img = new glif.Image(TEST_IMAGE);
        const lm = new glif.LightnessMap(img);
        assert.equal(lm.width, img.width);
        assert.equal(lm.height, img.height);
        lm.free();
        img.free();
    });

    it('full pipeline works', () => {
        if (!TEST_FONT) return;
        const img = new glif.Image(TEST_IMAGE);
        const lm = new glif.LightnessMap(img);
        const sc = new glif.SamplingConfig();
        const pm = new glif.PrecomputedMasks(sc, 10, 20, lm.width);
        const db = new glif.CharDatabase(TEST_FONT, 10, 20, sc);
        const grid = new glif.Grid(img, 10, 20);

        grid.computeVectorsFast(lm, pm);
        grid.computeColors(img);
        grid.applyDirectionalContrast(sc, 1.25);
        grid.applyGlobalContrast(1.5);
        grid.match(db);

        const cells = grid.getCells();
        assert.equal(cells.length, grid.rows * grid.cols);
        for (const cell of cells) {
            const code = cell.ch.charCodeAt(0);
            assert.ok(code >= 32 && code <= 126);
        }

        grid.free();
        db.free();
        pm.free();
        lm.free();
        img.free();
    });
});
