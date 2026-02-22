/**
 * glif-encoder.js — ES module wrapping the WASM .glif encoder.
 *
 * Mirrors glif-player.js pattern: mount() loads WASM, then call
 * init/encodeFrame/feedAudio/finish to produce a .glif ArrayBuffer.
 */

export class GlifEncoder {
    constructor() {
        this._module = null;
        this._mounted = false;
        this._initialized = false;
        this.onprogress = null;
        this._frameCount = 0;
    }

    /**
     * Load the WASM encoder module.
     */
    async mount() {
        if (this._mounted) return;

        if (typeof createGlifEncoder === 'undefined') {
            await new Promise((resolve, reject) => {
                const s = document.createElement('script');
                s.src = new URL('./glif-encoder-wasm.js', import.meta.url).href;
                s.onload = resolve;
                s.onerror = reject;
                document.head.appendChild(s);
            });
        }
        this._module = await createGlifEncoder();
        this._mounted = true;
    }

    /**
     * Initialize the encoder pipeline.
     * @param {Object} opts
     * @param {number} opts.width - Input video width
     * @param {number} opts.height - Input video height
     * @param {number} [opts.cellW=10] - Cell width
     * @param {number} [opts.cellH=20] - Cell height
     * @param {number} [opts.fps=30] - Framerate
     * @param {boolean} [opts.dark=true] - Dark mode
     * @param {boolean} [opts.compress=true] - Enable compression
     * @param {string} [opts.preset] - 'lo' or 'hi' (overrides cell size)
     * @param {number} [opts.dirCrunch=1.25] - Directional contrast crunch
     * @param {number} [opts.globalCrunch=1.5] - Global contrast crunch
     * @param {number} [opts.audioRate=8000] - Crushed audio sample rate
     * @param {number} [opts.audioDepth=8] - Crushed audio bit depth
     * @param {ArrayBuffer} [opts.fontData] - Custom TTF font data
     */
    init(opts) {
        if (!this._mounted) throw new Error('Call mount() first');

        let cellW = opts.cellW || 10;
        let cellH = opts.cellH || 20;
        if (opts.preset === 'lo') { cellW = 16; cellH = 32; }
        if (opts.preset === 'hi') { cellW = 8; cellH = 16; }

        const dark = opts.dark !== undefined ? (opts.dark ? 1 : 0) : 1;
        const compress = opts.compress !== undefined ? (opts.compress ? 1 : 0) : 1;
        const dirCrunch = opts.dirCrunch || 1.25;
        const globalCrunch = opts.globalCrunch || 1.5;
        const audioRate = opts.audioRate || 0;
        const audioDepth = opts.audioDepth || 0;

        let fontPtr = 0, fontLen = 0;
        if (opts.fontData) {
            const fontBytes = new Uint8Array(opts.fontData);
            fontPtr = this._module._malloc(fontBytes.length);
            this._module.HEAPU8.set(fontBytes, fontPtr);
            fontLen = fontBytes.length;
        }

        const result = this._module._encoder_init(
            opts.width, opts.height, cellW, cellH,
            opts.fps || 30, dark, compress,
            fontPtr, fontLen,
            dirCrunch, globalCrunch,
            audioRate, audioDepth
        );

        if (fontPtr) this._module._free(fontPtr);
        if (result !== 0) throw new Error('Failed to initialize encoder');

        this._initialized = true;
        this._frameCount = 0;
    }

    /**
     * Encode a single RGB24 frame.
     * @param {ArrayBuffer} rgb24ArrayBuffer - Raw RGB24 pixel data
     */
    encodeFrame(rgb24ArrayBuffer) {
        if (!this._initialized) throw new Error('Call init() first');

        const data = new Uint8Array(rgb24ArrayBuffer);
        const ptr = this._module._malloc(data.length);
        this._module.HEAPU8.set(data, ptr);

        // Width/height are embedded in the encoder state; just pass the buffer
        const result = this._module._encoder_frame(
            ptr, 0, 0 // width/height unused — encoder knows dimensions
        );
        this._module._free(ptr);

        if (result !== 0) throw new Error('Failed to encode frame');

        this._frameCount++;
        if (this.onprogress) this.onprogress(this._frameCount);
    }

    /**
     * Feed crushed PCM audio samples.
     * @param {Int16Array} pcmInt16Array - Mono s16le PCM samples
     */
    feedAudio(pcmInt16Array) {
        if (!this._initialized) return;

        const numSamples = pcmInt16Array.length;
        const byteLen = numSamples * 2;
        const ptr = this._module._malloc(byteLen);
        const heap16 = new Int16Array(this._module.HEAP16.buffer, ptr, numSamples);
        heap16.set(pcmInt16Array);

        this._module._encoder_audio_samples(ptr, numSamples);
        this._module._free(ptr);
    }

    /**
     * Embed original audio (Opus/OGG/AAC bitstream).
     * @param {ArrayBuffer} opusArrayBuffer
     */
    setOriginalAudio(opusArrayBuffer) {
        if (!this._initialized) return;

        const data = new Uint8Array(opusArrayBuffer);
        const ptr = this._module._malloc(data.length);
        this._module.HEAPU8.set(data, ptr);

        this._module._encoder_keep_original_audio(ptr, data.length);
        this._module._free(ptr);
    }

    /**
     * Finalize encoding and return the .glif binary.
     * @returns {ArrayBuffer} The .glif file data
     */
    async finish() {
        if (!this._initialized) throw new Error('Not initialized');

        const result = this._module._encoder_finish();
        if (result !== 0) throw new Error('Failed to finalize encoding');

        const outPtr = this._module._encoder_get_output_ptr();
        const outLen = this._module._encoder_get_output_len();

        if (!outPtr || outLen <= 0) throw new Error('No output produced');

        const output = new Uint8Array(outLen);
        output.set(this._module.HEAPU8.subarray(outPtr, outPtr + outLen));

        this._initialized = false;
        return output.buffer;
    }
}
