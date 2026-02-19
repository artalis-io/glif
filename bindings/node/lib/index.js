'use strict';

const native = require('../build/Release/glif_native.node');

/**
 * Render an image to ASCII art.
 *
 * @param {string} imagePath - Path to input image
 * @param {string} fontPath - Path to monospace TTF font
 * @param {Object} [options]
 * @param {number} [options.cellWidth=10] - Cell width in pixels
 * @param {number} [options.cellHeight=20] - Cell height in pixels
 * @param {number} [options.dirCrunch=1.25] - Directional contrast exponent
 * @param {number} [options.globalCrunch=1.5] - Global contrast exponent
 * @returns {{ rows: number, cols: number, cells: Array<{ch: string, r: number, g: number, b: number}>, toPlainText: Function, toAnsi: Function }}
 */
function render(imagePath, fontPath, options) {
    const opts = options || {};
    const cellW = opts.cellWidth || 10;
    const cellH = opts.cellHeight || 20;
    const dirCrunch = opts.dirCrunch || 1.25;
    const globalCrunch = opts.globalCrunch || 1.5;

    const result = native.render(imagePath, fontPath, cellW, cellH, dirCrunch, globalCrunch);

    result.toPlainText = function () {
        const lines = [];
        for (let r = 0; r < this.rows; r++) {
            const offset = r * this.cols;
            let line = '';
            for (let c = 0; c < this.cols; c++) {
                line += this.cells[offset + c].ch;
            }
            lines.push(line);
        }
        return lines.join('\n');
    };

    result.toAnsi = function () {
        const parts = [];
        for (let r = 0; r < this.rows; r++) {
            const offset = r * this.cols;
            for (let c = 0; c < this.cols; c++) {
                const cell = this.cells[offset + c];
                parts.push(`\x1b[38;2;${cell.r};${cell.g};${cell.b}m${cell.ch}`);
            }
            parts.push('\x1b[0m\n');
        }
        return parts.join('');
    };

    return result;
}

module.exports = {
    render,
    // Low-level API
    Image: native.Image,
    LightnessMap: native.LightnessMap,
    SamplingConfig: native.SamplingConfig,
    PrecomputedMasks: native.PrecomputedMasks,
    CharDatabase: native.CharDatabase,
    Grid: native.Grid,
};
