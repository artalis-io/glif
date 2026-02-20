/**
 * glif-player.js — ES module wrapping the WASM .glif player with playback controls.
 */

export class GlifPlayer {
    constructor() {
        this._module = null;
        this._canvas = null;
        this._mounted = false;

        // Playback state
        this._playing = false;
        this._currentFrame = 0;
        this._speed = 1.0;
        this._loop = true;
        this._rafId = null;
        this._lastTime = 0;
        this._accumulator = 0;

        // Header cache
        this._frames = 0;
        this._fps = 30;
        this._cols = 0;
        this._rows = 0;
        this._cellW = 0;
        this._cellH = 0;
        this._flags = 0;

        // HDR
        this._hdrIntensity = 0;

        // Callbacks
        this.onload = null;
        this.onframe = null;
        this.onend = null;
    }

    get frames() { return this._frames; }
    get fps() { return this._fps; }
    get cols() { return this._cols; }
    get rows() { return this._rows; }
    get cellW() { return this._cellW; }
    get cellH() { return this._cellH; }
    get flags() { return this._flags; }
    get currentFrame() { return this._currentFrame; }
    get isPlaying() { return this._playing; }

    /**
     * Initialize WASM module and WebGL context.
     * @param {HTMLCanvasElement} canvasElement
     */
    async mount(canvasElement) {
        if (this._mounted) return;

        this._canvas = canvasElement;

        // Ensure the canvas has the expected ID for Emscripten
        if (!canvasElement.id) {
            canvasElement.id = 'glif-player-canvas';
        }

        // Load WASM module — Emscripten UMD output needs a classic script tag
        if (typeof createGlifPlayer === 'undefined') {
            await new Promise((resolve, reject) => {
                const s = document.createElement('script');
                s.src = new URL('./glif-player-wasm.js', import.meta.url).href;
                s.onload = resolve;
                s.onerror = reject;
                document.head.appendChild(s);
            });
        }
        this._module = await createGlifPlayer();

        const dpr = window.devicePixelRatio || 1;
        const w = canvasElement.width;
        const h = canvasElement.height;

        this._module._player_init(dpr, w, h);
        if (this._hdrIntensity > 0) this._module._player_set_hdr(this._hdrIntensity);
        this._mounted = true;
    }

    /**
     * Load a .glif file from an ArrayBuffer.
     * @param {ArrayBuffer} arrayBuffer
     */
    async loadFile(arrayBuffer) {
        if (!this._mounted) throw new Error('Call mount() first');

        this.pause();

        const data = new Uint8Array(arrayBuffer);
        const ptr = this._module._malloc(data.length);
        this._module.HEAPU8.set(data, ptr);

        const result = this._module._player_load(ptr, data.length);
        this._module._free(ptr);

        if (result !== 0) throw new Error('Failed to parse .glif file');

        // Cache header values
        this._frames = this._module._player_get_frames();
        this._fps = this._module._player_get_fps();
        this._cols = this._module._player_get_cols();
        this._rows = this._module._player_get_rows();
        this._cellW = this._module._player_get_cell_w();
        this._cellH = this._module._player_get_cell_h();
        this._flags = this._module._player_get_flags();

        // Decode and render first frame
        this._currentFrame = 0;
        this._module._player_decode_frame(0);
        this._module._player_render();

        if (this.onload) this.onload();
    }

    /**
     * Start playback.
     */
    play() {
        if (this._playing || this._frames === 0) return;
        this._playing = true;
        this._lastTime = performance.now();
        this._accumulator = 0;
        this._tick = this._tick.bind(this);
        this._rafId = requestAnimationFrame(this._tick);
    }

    /**
     * Pause playback.
     */
    pause() {
        this._playing = false;
        if (this._rafId !== null) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    }

    /**
     * Seek to a specific frame.
     * @param {number} frame
     */
    seek(frame) {
        if (!this._mounted || this._frames === 0) return;
        frame = Math.max(0, Math.min(frame, this._frames - 1));
        this._currentFrame = frame;
        this._module._player_decode_frame(frame);
        this._module._player_render();
        if (this.onframe) this.onframe(frame);
    }

    /**
     * Set playback speed multiplier.
     * @param {number} multiplier
     */
    setSpeed(multiplier) {
        this._speed = Math.max(0.1, Math.min(multiplier, 10));
    }

    /**
     * Set HDR tone mapping intensity.
     * @param {number} intensity - 0.0 (off) to 1.0 (full effect)
     */
    setHDR(intensity) {
        this._hdrIntensity = Math.max(0, Math.min(1, intensity));
        if (this._module) this._module._player_set_hdr(this._hdrIntensity);
    }

    /**
     * Clean up all resources.
     */
    destroy() {
        this.pause();
        if (this._module) {
            this._module._player_free();
        }
        this._mounted = false;
        this._module = null;
        this._canvas = null;
    }

    /**
     * Resize the player canvas.
     * @param {number} w - width in physical pixels
     * @param {number} h - height in physical pixels
     */
    resize(w, h) {
        if (!this._mounted) return;
        this._module._player_resize(w, h);
        // Re-render current frame at new size
        this._module._player_render();
    }

    /** @private */
    _tick(now) {
        if (!this._playing) return;

        const dt = (now - this._lastTime) / 1000; // seconds
        this._lastTime = now;
        this._accumulator += dt * this._speed;

        const frameDuration = 1 / this._fps;
        let advanced = false;

        while (this._accumulator >= frameDuration) {
            this._accumulator -= frameDuration;
            this._currentFrame++;

            if (this._currentFrame >= this._frames) {
                if (this._loop) {
                    this._currentFrame = 0;
                } else {
                    this._currentFrame = this._frames - 1;
                    this._playing = false;
                    if (this.onend) this.onend();
                    return;
                }
            }
            advanced = true;
        }

        if (advanced) {
            this._module._player_decode_frame(this._currentFrame);
            this._module._player_render();
            if (this.onframe) this.onframe(this._currentFrame);
        }

        this._rafId = requestAnimationFrame(this._tick);
    }
}
