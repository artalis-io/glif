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

        // Audio (crushed PCM)
        this._audioCtx = null;
        this._audioEnabled = false;
        this._audioMuted = false;
        this._audioBuffer = null;    // decoded AudioBuffer
        this._audioSource = null;    // current AudioBufferSourceNode
        this._audioGain = null;
        this._audioStartTime = 0;    // audioCtx.currentTime when source started
        this._audioStartOffset = 0;  // offset in seconds when source started

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
    get hasAudio() { return this._audioEnabled; }

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

        // Check for audio
        this._audioEnabled = !!this._module._player_has_audio();

        // Decode and render first frame
        this._currentFrame = 0;
        this._module._player_decode_frame(0);
        this._module._player_render();

        // Build AudioBuffer from crushed PCM
        if (this._audioEnabled) {
            this._buildAudioBuffer();
        }

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
        this._startAudio();
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
        this._stopAudio();
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
        if (this._playing) {
            this._stopAudio();
            this._startAudio();
        }
        if (this.onframe) this.onframe(frame);
    }

    /**
     * Set playback speed multiplier.
     * @param {number} multiplier
     */
    setSpeed(multiplier) {
        this._speed = Math.max(0.1, Math.min(multiplier, 10));
        if (this._audioSource) {
            this._audioSource.playbackRate.value = this._speed;
        }
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
        if (this._audioCtx) {
            this._audioCtx.close();
            this._audioCtx = null;
        }
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

    /**
     * Toggle audio mute.
     */
    toggleMute() {
        this._audioMuted = !this._audioMuted;
        if (this._audioGain) {
            this._audioGain.gain.value = this._audioMuted ? 0 : 1;
        }
    }

    /** @private — Build AudioBuffer from crushed PCM in WASM memory */
    _buildAudioBuffer() {
        try {
            if (!this._audioCtx) {
                this._audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            }
            this._audioGain = this._audioCtx.createGain();
            this._audioGain.gain.value = this._audioMuted ? 0 : 1;
            this._audioGain.connect(this._audioCtx.destination);

            const pcmPtr = this._module._player_get_audio_pcm_ptr();
            const pcmLen = this._module._player_get_audio_pcm_len();
            const sampleRate = this._module._player_get_audio_sample_rate();
            const bitDepth = this._module._player_get_audio_bit_depth();

            if (!pcmPtr || pcmLen <= 0 || sampleRate <= 0) {
                this._audioEnabled = false;
                return;
            }

            // Create AudioBuffer
            this._audioBuffer = this._audioCtx.createBuffer(1, pcmLen, sampleRate);
            const channel = this._audioBuffer.getChannelData(0);

            // Convert unsigned 8-bit (or 4-bit) PCM to float32 [-1, 1]
            const maxVal = (bitDepth === 4) ? 15 : 255;
            const mid = maxVal / 2;
            for (let i = 0; i < pcmLen; i++) {
                const sample = this._module.HEAPU8[pcmPtr + i];
                channel[i] = (sample - mid) / mid;
            }
        } catch (e) {
            this._audioEnabled = false;
        }
    }

    /** @private — Start audio playback from current video position */
    _startAudio() {
        if (!this._audioEnabled || !this._audioCtx || !this._audioBuffer || this._audioMuted) return;

        if (this._audioCtx.state === 'suspended') {
            this._audioCtx.resume();
        }

        this._stopAudio();

        const offset = this._currentFrame / this._fps;
        if (offset >= this._audioBuffer.duration) return;

        this._audioSource = this._audioCtx.createBufferSource();
        this._audioSource.buffer = this._audioBuffer;
        this._audioSource.playbackRate.value = this._speed;
        this._audioSource.loop = this._loop;
        this._audioSource.connect(this._audioGain);
        this._audioSource.start(0, offset);
        this._audioStartTime = this._audioCtx.currentTime;
        this._audioStartOffset = offset;
    }

    /** @private — Stop audio playback */
    _stopAudio() {
        if (this._audioSource) {
            try { this._audioSource.stop(); } catch (e) { /* already stopped */ }
            this._audioSource.disconnect();
            this._audioSource = null;
        }
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
                    this._stopAudio();
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
