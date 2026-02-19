/**
 * Render an image to ASCII art and print to the terminal.
 *
 * Usage:
 *   node render.mjs <image> <font> [--color] [--cell-width N] [--cell-height N]
 *
 * Examples:
 *   npm start                          # renders raccoon with default font
 *   node render.mjs ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf
 *   node render.mjs ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf --color
 *   node render.mjs ../../images/raccoon.jpg ../../fonts/GeistMono-Regular.ttf --color --cell-width 8 --cell-height 16
 */

import { createRequire } from 'node:module';
import { parseArgs } from 'node:util';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const glif = require('@artalis/glif');

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
        color:         { type: 'boolean', default: false },
        'cell-width':  { type: 'string', default: '10' },
        'cell-height': { type: 'string', default: '20' },
        'dir-crunch':  { type: 'string', default: '1.25' },
        'global-crunch': { type: 'string', default: '1.5' },
    },
});

const imagePath = positionals[0] || path.join(__dirname, '..', '..', '..', 'images', 'raccoon.jpg');
const fontPath = positionals[1] || path.join(__dirname, '..', '..', '..', 'fonts', 'GeistMono-Regular.ttf');

const result = glif.render(imagePath, fontPath, {
    cellWidth: parseInt(values['cell-width']),
    cellHeight: parseInt(values['cell-height']),
    dirCrunch: parseFloat(values['dir-crunch']),
    globalCrunch: parseFloat(values['global-crunch']),
});

if (values.color) {
    process.stdout.write(result.toAnsi());
} else {
    console.log(result.toPlainText());
}

process.stderr.write(`\n${result.cols}x${result.rows} grid\n`);
