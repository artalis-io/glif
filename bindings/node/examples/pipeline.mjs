/**
 * Demonstrates the low-level pipeline API with step-by-step rendering.
 *
 * Usage:
 *   node pipeline.mjs <image> <font>
 *
 * Example:
 *   node pipeline.mjs ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
 */

import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const glif = require('@artalis/glif');

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const imagePath = process.argv[2] || path.join(__dirname, '..', '..', '..', 'images', 'raccoon.jpg');
const fontPath = process.argv[3] || path.join(__dirname, '..', '..', '..', 'fonts', 'GeistMono-Regular.ttf');

const cellW = 10;
const cellH = 20;

// Step 1: Load image
const img = new glif.Image(imagePath);
process.stderr.write(`Loaded image: ${img.width}x${img.height} (${img.channels} channels)\n`);

// Step 2: Compute lightness
const lm = new glif.LightnessMap(img);
process.stderr.write(`Lightness map: ${lm.width}x${lm.height}\n`);

// Step 3: Sampling setup
const sc = new glif.SamplingConfig();
const pm = new glif.PrecomputedMasks(sc, cellW, cellH, lm.width);

// Step 4: Font database
const db = new glif.CharDatabase(fontPath, cellW, cellH, sc);

// Step 5: Grid
const grid = new glif.Grid(img, cellW, cellH);
process.stderr.write(`Grid: ${grid.cols}x${grid.rows} cells\n`);

// Step 6-10: Pipeline
grid.computeVectorsFast(lm, pm);
grid.computeColors(img);
grid.applyDirectionalContrast(sc, 1.25);
grid.applyGlobalContrast(1.5);
grid.match(db);

const cells = grid.getCells();
process.stderr.write(`Matched ${cells.length} cells\n`);

// Print ANSI output
for (let r = 0; r < grid.rows; r++) {
    const offset = r * grid.cols;
    let line = '';
    for (let c = 0; c < grid.cols; c++) {
        const cell = cells[offset + c];
        line += `\x1b[38;2;${cell.r};${cell.g};${cell.b}m${cell.ch}`;
    }
    process.stdout.write(line + '\x1b[0m\n');
}

// Cleanup
grid.free();
db.free();
pm.free();
lm.free();
img.free();
