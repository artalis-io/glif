/**
 * GlifRenderer — JS wrapper around the WASM glif module.
 *
 * Usage:
 *   const renderer = await GlifRenderer.create(fontArrayBuffer, { cellW: 10, cellH: 20 });
 *   const result = renderer.processFrame(rgbaPixels, width, height, 4);
 *   // result = { rows, cols, chars: Uint8Array, r, g, b }
 *   renderer.destroy();
 */
class GlifRenderer {
    constructor(module, ctx, fontPtr, fontLen) {
        this._module = module;
        this._ctx = ctx;
        this._fontPtr = fontPtr;
        this._fontLen = fontLen;
        this._inputPtr = 0;
        this._inputSize = 0;
        this._charsPtr = 0;
        this._rPtr = 0;
        this._gPtr = 0;
        this._bPtr = 0;
        this._outputSize = 0;
    }

    /**
     * Create a GlifRenderer.
     * @param {ArrayBuffer} fontArrayBuffer — raw TTF font file
     * @param {Object} opts
     * @param {number} opts.cellW — cell width in pixels (default 10)
     * @param {number} opts.cellH — cell height in pixels (default 20)
     * @param {number} opts.dirCrunch — directional contrast exponent (default 1.25)
     * @param {number} opts.globalCrunch — global contrast exponent (default 1.5)
     * @returns {Promise<GlifRenderer>}
     */
    static async create(fontArrayBuffer, opts = {}) {
        const cellW = opts.cellW || 10;
        const cellH = opts.cellH || 20;
        const dirCrunch = opts.dirCrunch ?? 1.25;
        const globalCrunch = opts.globalCrunch ?? 1.5;

        const module = await createGlifModule();

        const fontBytes = new Uint8Array(fontArrayBuffer);
        const fontPtr = module._malloc(fontBytes.length);
        module.HEAPU8.set(fontBytes, fontPtr);

        const ctx = module._glif_init(fontPtr, fontBytes.length,
                                      cellW, cellH, dirCrunch, globalCrunch);
        if (!ctx) {
            module._free(fontPtr);
            throw new Error('glif_init failed — invalid font data?');
        }

        return new GlifRenderer(module, ctx, fontPtr, fontBytes.length);
    }

    /**
     * Process a single frame.
     * @param {Uint8Array|Uint8ClampedArray} pixels — pixel data (RGB or RGBA)
     * @param {number} w — image width
     * @param {number} h — image height
     * @param {number} channels — 3 (RGB) or 4 (RGBA)
     * @returns {{ rows: number, cols: number, chars: Uint8Array, r: Uint8Array, g: Uint8Array, b: Uint8Array }}
     */
    processFrame(pixels, w, h, channels) {
        const M = this._module;
        const inputSize = w * h * channels;

        // Grow input buffer if needed
        if (inputSize > this._inputSize) {
            if (this._inputPtr) M._free(this._inputPtr);
            this._inputPtr = M._malloc(inputSize);
            this._inputSize = inputSize;
        }

        // Copy pixels to WASM heap
        M.HEAPU8.set(pixels.length <= inputSize ? pixels : pixels.subarray(0, inputSize),
                     this._inputPtr);

        // Estimate max output size and grow if needed
        const maxCells = Math.ceil(w / 2) * Math.ceil(h / 2); // conservative upper bound
        if (maxCells > this._outputSize) {
            if (this._charsPtr) {
                M._free(this._charsPtr);
                M._free(this._rPtr);
                M._free(this._gPtr);
                M._free(this._bPtr);
            }
            this._charsPtr = M._malloc(maxCells);
            this._rPtr = M._malloc(maxCells);
            this._gPtr = M._malloc(maxCells);
            this._bPtr = M._malloc(maxCells);
            this._outputSize = maxCells;
        }

        const packed = M._glif_process_frame(
            this._ctx, this._inputPtr, w, h, channels,
            this._charsPtr, this._rPtr, this._gPtr, this._bPtr
        );

        if (packed === 0) return null;

        const rows = packed & 0xFFFF;
        const cols = (packed >> 16) & 0xFFFF;
        const ncells = rows * cols;

        return {
            rows,
            cols,
            chars: new Uint8Array(M.HEAPU8.buffer, this._charsPtr, ncells),
            r: new Uint8Array(M.HEAPU8.buffer, this._rPtr, ncells),
            g: new Uint8Array(M.HEAPU8.buffer, this._gPtr, ncells),
            b: new Uint8Array(M.HEAPU8.buffer, this._bPtr, ncells),
        };
    }

    /**
     * Generate a DPR-aware font atlas canvas for WebGL rendering.
     * Renders ASCII 32–126 (95 chars) in a 16×6 grid at physical resolution.
     * @param {string} fontFamily — CSS font-family string
     * @param {number} fontSize — font size in CSS pixels (logical)
     * @returns {{ canvas: HTMLCanvasElement, charW: number, charH: number, physCharW: number, physCharH: number }}
     */
    static generateFontAtlas(fontFamily, fontSize) {
        const dpr = window.devicePixelRatio || 1;
        const renderSize = Math.round(fontSize * dpr);
        const atlasGridX = 16;
        const atlasGridY = 6;
        const physCharW = Math.ceil(renderSize * 0.6);
        const physCharH = renderSize;
        const atlasW = atlasGridX * physCharW;
        const atlasH = atlasGridY * physCharH;

        const canvas = document.createElement('canvas');
        canvas.width = atlasW;
        canvas.height = atlasH;
        const ctx = canvas.getContext('2d');

        // Clear to transparent black
        ctx.clearRect(0, 0, atlasW, atlasH);

        ctx.font = `${renderSize}px ${fontFamily}`;
        ctx.textBaseline = 'top';
        ctx.fillStyle = '#fff';

        // Render ASCII 32–126 into grid cells
        for (let i = 0; i < 95; i++) {
            const ch = String.fromCharCode(32 + i);
            const col = i % atlasGridX;
            const row = Math.floor(i / atlasGridX);
            ctx.fillText(ch, col * physCharW, row * physCharH);
        }

        // Return logical sizes for layout, physical sizes for WebGL backing
        return {
            canvas,
            charW: physCharW / dpr,
            charH: physCharH / dpr,
            physCharW,
            physCharH
        };
    }

    destroy() {
        const M = this._module;
        if (this._ctx) {
            M._glif_free(this._ctx);
            this._ctx = 0;
        }
        if (this._fontPtr) { M._free(this._fontPtr); this._fontPtr = 0; }
        if (this._inputPtr) { M._free(this._inputPtr); this._inputPtr = 0; }
        if (this._charsPtr) {
            M._free(this._charsPtr);
            M._free(this._rPtr);
            M._free(this._gPtr);
            M._free(this._bPtr);
            this._charsPtr = 0;
        }
    }
}
