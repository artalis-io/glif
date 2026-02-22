/**
 * Glif App — Minimal JS bridge for browser APIs.
 *
 * Forwards mouse/touch/keyboard events to the C UI module,
 * handles file picking, webcam, video playback, export (PNG/PPM/SVG),
 * comparison, and DPR-aware canvas sizing.
 *
 * The bottom media bar (HDR, compare, export, playback controls) is
 * rendered in C via Clay/Nuklear. JS provides global callback functions
 * that C calls via EM_ASM when buttons are pressed.
 */
(function () {
    'use strict';

    const canvas = document.getElementById('canvas');
    const appWrap = document.getElementById('appWrap');
    const canvasArea = document.getElementById('canvasArea');
    const dropOverlay = document.getElementById('dropOverlay');
    const compareDivider = document.getElementById('compareDivider');
    const compareLabelL = document.getElementById('compareLabelL');
    const compareLabelR = document.getElementById('compareLabelR');

    let Module = null;
    let cameras = [];
    let currentCamera = -1;
    let videoEl = null;
    let videoStream = null;
    let videoAnimId = null;
    let lastDpr = 0;
    let isVideoMode = false;
    let hasContent = false;
    let compareOn = false;
    let sliderX = 0.5;
    let audioMuted = true;
    let audioAvailable = false;
    let originalImgSrc = null; /* URL for comparison of dropped images */

    /* ── DPR + resize ── */

    function resize() {
        const dpr = window.devicePixelRatio || 1;
        const w = canvas.clientWidth;
        const h = canvas.clientHeight;
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(h * dpr);
        if (Module) {
            if (dpr !== lastDpr && Module._app_set_dpr) {
                Module._app_set_dpr(dpr);
                lastDpr = dpr;
            }
            if (Module._app_resize) {
                Module._app_resize(canvas.width, canvas.height);
            }
        }
        if (compareOn) sizeCompare();
    }

    window.addEventListener('resize', resize);

    function watchDpr() {
        var mq = window.matchMedia('(resolution: ' + window.devicePixelRatio + 'dppx)');
        mq.addEventListener('change', function () {
            resize();
            watchDpr();
        }, { once: true });
    }
    watchDpr();

    /* ── Content mode (replaces old showControls) ── */

    function showControls(videoMode) {
        isVideoMode = videoMode;
        hasContent = true;
        dropOverlay.classList.add('hidden');
        if (Module && Module._app_set_content_mode) {
            Module._app_set_content_mode(1, videoMode ? 1 : 0);
        }
        requestAnimationFrame(function () { resize(); });
    }

    /* ── Mouse events ── */

    function mouseHandler(e) {
        if (!Module) return;
        const dpr = window.devicePixelRatio || 1;
        const rect = canvas.getBoundingClientRect();
        const x = (e.clientX - rect.left) * dpr;
        const y = (e.clientY - rect.top) * dpr;
        Module._app_mouse(Math.round(x / dpr), Math.round(y / dpr), e.buttons);
    }

    canvas.addEventListener('mousemove', mouseHandler);
    canvas.addEventListener('mousedown', mouseHandler);
    canvas.addEventListener('mouseup', mouseHandler);

    /* ── Touch events ── */

    function touchHandler(e) {
        if (!Module) return;
        e.preventDefault();
        const dpr = window.devicePixelRatio || 1;
        const rect = canvas.getBoundingClientRect();

        for (let i = 0; i < e.changedTouches.length; i++) {
            const t = e.changedTouches[i];
            const x = Math.round((t.clientX - rect.left));
            const y = Math.round((t.clientY - rect.top));
            let phase = 1;
            if (e.type === 'touchstart') phase = 0;
            else if (e.type === 'touchend' || e.type === 'touchcancel') phase = 2;
            Module._app_touch(t.identifier, x, y, phase);
        }
    }

    canvas.addEventListener('touchstart', touchHandler, { passive: false });
    canvas.addEventListener('touchmove', touchHandler, { passive: false });
    canvas.addEventListener('touchend', touchHandler, { passive: false });
    canvas.addEventListener('touchcancel', touchHandler, { passive: false });

    /* ── Keyboard ── */

    document.addEventListener('keydown', function (e) {
        if (e.target.tagName === 'SELECT') return;
        if (Module) Module._app_key(e.keyCode, 1);

        switch (e.key) {
            case 's': case 'S':
                if (hasContent) doExport();
                break;
            case 'f': case 'F':
                toggleFullscreen();
                break;
            case 'h': case 'H':
                if (Module && Module._app_toggle_hdr) Module._app_toggle_hdr();
                break;
            case 'c': case 'C':
                glifToggleCompare(!compareOn);
                break;
            case 'a': case 'A':
                if (isVideoMode) glifToggleAudio();
                break;
            case ' ':
                if (isVideoMode) {
                    e.preventDefault();
                    glifPlayPause();
                }
                break;
            case 'ArrowLeft':
                if (isVideoMode && videoEl) {
                    videoEl.currentTime = Math.max(0, videoEl.currentTime - 5);
                }
                break;
            case 'ArrowRight':
                if (isVideoMode && videoEl) {
                    videoEl.currentTime = Math.min(videoEl.duration || 0, videoEl.currentTime + 5);
                }
                break;
        }
    });
    document.addEventListener('keyup', function (e) {
        if (Module) Module._app_key(e.keyCode, 0);
    });

    /* ── Global callbacks (called from C via EM_ASM) ── */

    function glifPlayPause() {
        if (!videoEl || !isVideoMode) return;
        if (videoEl.paused) videoEl.play();
        else videoEl.pause();
    }
    window.glifPlayPause = glifPlayPause;

    window.glifSeek = function (t) {
        if (!videoEl) return;
        videoEl.currentTime = t;
    };

    window.glifSetSpeed = function (rate) {
        if (!videoEl) return;
        videoEl.playbackRate = rate;
    };

    function glifToggleAudio() {
        if (!videoEl || !isVideoMode) return;
        audioMuted = !audioMuted;
        videoEl.muted = audioMuted;
    }
    window.glifToggleAudio = glifToggleAudio;

    function glifToggleCompare(on) {
        if (!hasContent || !Module) return;
        compareOn = !!on;
        canvasArea.classList.toggle('compare-active', compareOn);
        compareDivider.style.display = compareOn ? 'block' : 'none';
        compareLabelL.style.display = compareOn ? 'block' : 'none';
        compareLabelR.style.display = compareOn ? 'block' : 'none';
        Module._app_set_compare(compareOn ? 1 : 0, sliderX);
        if (compareOn) {
            if (!isVideoMode) uploadImageForCompare();
            sizeCompare();
        }
    }
    window.glifToggleCompare = glifToggleCompare;

    window.glifExport = function (fmtIdx) {
        if (!hasContent) return;
        if (fmtIdx === 1) exportPPM();
        else if (fmtIdx === 2) exportSVG();
        else exportPNG();
    };

    function toggleFullscreen() {
        if (document.fullscreenElement) {
            document.exitFullscreen();
        } else {
            appWrap.requestFullscreen().catch(function () {});
        }
    }
    window.glifToggleFullscreen = toggleFullscreen;

    document.addEventListener('fullscreenchange', function () { resize(); });

    /* ── Comparison (WebGL split) ── */

    var contentRectPtr = 0;
    function getContentRect() {
        if (!Module) return null;
        if (!contentRectPtr) contentRectPtr = Module._malloc(16);
        Module._app_get_content_rect(contentRectPtr);
        var view = new Float32Array(Module.HEAPU8.buffer, contentRectPtr, 4);
        return { x: view[0], y: view[1], w: view[2], h: view[3] };
    }

    function sizeCompare() {
        var cr = getContentRect();
        if (!cr || cr.w <= 0) return;
        var cRect = canvas.getBoundingClientRect();
        var aRect = canvasArea.getBoundingClientRect();
        var ox = cRect.left - aRect.left;
        var oy = cRect.top - aRect.top;
        var contentLeft = ox + cr.x;
        var contentTop = oy + cr.y;
        compareDivider.style.left = (contentLeft + cr.w * sliderX) + 'px';
        compareDivider.style.top = contentTop + 'px';
        compareDivider.style.height = cr.h + 'px';
        compareLabelL.style.top = (contentTop + 8) + 'px';
        compareLabelL.style.left = (contentLeft + 8) + 'px';
        compareLabelR.style.top = (contentTop + 8) + 'px';
        compareLabelR.style.right = (aRect.width - contentLeft - cr.w + 8) + 'px';
    }

    function uploadImageForCompare() {
        if (!originalImgSrc || !Module) return;
        var img = new Image();
        img.onload = function () {
            var c = document.createElement('canvas');
            c.width = img.naturalWidth || img.width;
            c.height = img.naturalHeight || img.height;
            var ctx = c.getContext('2d');
            ctx.drawImage(img, 0, 0);
            var data = ctx.getImageData(0, 0, c.width, c.height);
            var ptr = Module._malloc(data.data.length);
            Module.HEAPU8.set(data.data, ptr);
            Module._app_upload_video_frame(ptr, c.width, c.height);
            Module._free(ptr);
        };
        img.src = originalImgSrc;
    }

    /* Draggable split: click/drag on canvasArea moves split position */
    (function () {
        var dragging = false;
        function updateSplit(clientX) {
            if (!compareOn || !Module) return;
            var cr = getContentRect();
            if (!cr || cr.w <= 0) return;
            var cRect = canvas.getBoundingClientRect();
            var contentLeft = cRect.left + cr.x;
            sliderX = Math.max(0, Math.min(1, (clientX - contentLeft) / cr.w));
            Module._app_set_compare(1, sliderX);
            sizeCompare();
        }
        canvasArea.addEventListener('mousedown', function (e) {
            if (!compareOn) return;
            var cr = getContentRect();
            if (!cr || cr.w <= 0) return;
            var cRect = canvas.getBoundingClientRect();
            var relX = e.clientX - cRect.left;
            var relY = e.clientY - cRect.top;
            if (relX < cr.x || relX > cr.x + cr.w ||
                relY < cr.y || relY > cr.y + cr.h) return;
            dragging = true;
            updateSplit(e.clientX);
            e.preventDefault();
        });
        canvasArea.addEventListener('touchstart', function (e) {
            if (!compareOn) return;
            var cr = getContentRect();
            if (!cr || cr.w <= 0) return;
            var cRect = canvas.getBoundingClientRect();
            var t = e.touches[0];
            var relX = t.clientX - cRect.left;
            var relY = t.clientY - cRect.top;
            if (relX < cr.x || relX > cr.x + cr.w ||
                relY < cr.y || relY > cr.y + cr.h) return;
            dragging = true;
            updateSplit(t.clientX);
            e.preventDefault();
        }, { passive: false });
        document.addEventListener('mousemove', function (e) { if (dragging) updateSplit(e.clientX); });
        document.addEventListener('touchmove', function (e) {
            if (dragging) { updateSplit(e.touches[0].clientX); e.preventDefault(); }
        }, { passive: false });
        document.addEventListener('mouseup', function () { dragging = false; });
        document.addEventListener('touchend', function () { dragging = false; });
    })();

    /* ── Export ── */

    function readCanvasPixels() {
        if (Module && Module._app_frame) Module._app_frame();
        var gl = canvas.getContext('webgl');
        if (!gl) return null;
        var w = canvas.width, h = canvas.height;
        var pixels = new Uint8Array(w * h * 4);
        gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        return { pixels: pixels, w: w, h: h };
    }

    function triggerDownload(blob, filename) {
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.click();
        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    }

    function exportPNG() {
        var data = readCanvasPixels();
        if (!data) return;
        var c2 = document.createElement('canvas');
        c2.width = data.w; c2.height = data.h;
        var ctx = c2.getContext('2d');
        var img = ctx.createImageData(data.w, data.h);
        for (var y = 0; y < data.h; y++) {
            var srcRow = (data.h - 1 - y) * data.w * 4;
            var dstRow = y * data.w * 4;
            img.data.set(data.pixels.subarray(srcRow, srcRow + data.w * 4), dstRow);
        }
        ctx.putImageData(img, 0, 0);
        c2.toBlob(function (blob) {
            if (blob) triggerDownload(blob, 'glif-export.png');
        }, 'image/png');
    }

    function exportPPM() {
        var data = readCanvasPixels();
        if (!data) return;
        var w = data.w, h = data.h;
        var header = 'P6\n' + w + ' ' + h + '\n255\n';
        var headerBytes = new TextEncoder().encode(header);
        var rgb = new Uint8Array(w * h * 3);
        for (var y = 0; y < h; y++) {
            var srcY = h - 1 - y;
            for (var x = 0; x < w; x++) {
                var si = (srcY * w + x) * 4;
                var di = (y * w + x) * 3;
                rgb[di] = data.pixels[si];
                rgb[di + 1] = data.pixels[si + 1];
                rgb[di + 2] = data.pixels[si + 2];
            }
        }
        var blob = new Blob([headerBytes, rgb], { type: 'application/octet-stream' });
        triggerDownload(blob, 'glif-export.ppm');
    }

    function base64Encode(bytes) {
        var binary = '';
        for (var i = 0; i < bytes.length; i++) {
            binary += String.fromCharCode(bytes[i]);
        }
        return btoa(binary);
    }

    function exportSVG() {
        if (!Module) return;
        var rows = Module._app_get_grid_rows();
        var cols = Module._app_get_grid_cols();
        if (rows <= 0 || cols <= 0) return;
        var cellW = Module._app_get_grid_cell_w();
        var cellH = Module._app_get_grid_cell_h();

        /* Read grid data */
        var n = rows * cols;
        var ptr = Module._malloc(n * 4);
        Module._app_export_grid(ptr);
        var buf = new Uint8Array(Module.HEAPU8.buffer, ptr, n * 4);
        var grid = new Uint8Array(buf);
        Module._free(ptr);

        /* Read embedded font for SVG @font-face */
        var fontB64 = '';
        var fontPtr = Module._app_get_font_ptr();
        var fontLen = Module._app_get_font_len();
        if (fontPtr && fontLen > 0) {
            var fontBytes = new Uint8Array(Module.HEAPU8.buffer, fontPtr, fontLen);
            fontB64 = base64Encode(fontBytes);
        }

        var svgW = cols * cellW;
        var svgH = rows * cellH;
        var parts = [];
        parts.push('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + svgW + ' ' + svgH + '">');
        parts.push('<style>');
        if (fontB64) {
            parts.push("@font-face { font-family: 'GlifFont'; src: url(data:font/ttf;base64," + fontB64 + "); }");
            parts.push("text { font-family: 'GlifFont'; font-size: " + cellH + "px; dominant-baseline: text-before-edge; }");
        } else {
            parts.push("text { font-family: monospace; font-size: " + cellH + "px; dominant-baseline: text-before-edge; }");
        }
        parts.push('</style>');
        parts.push('<rect width="100%" height="100%" fill="#000"/>');

        for (var r = 0; r < rows; r++) {
            var y = r * cellH;
            /* Group same-color runs to reduce file size */
            var c = 0;
            while (c < cols) {
                var i = (r * cols + c) * 4;
                var ch = grid[i];
                if (ch === 32) { c++; continue; } /* skip spaces */
                var cr = grid[i + 1], cg = grid[i + 2], cb = grid[i + 3];
                var x = c * cellW;
                parts.push('<text x="' + x + '" y="' + y + '" fill="rgb(' + cr + ',' + cg + ',' + cb + ')">');
                /* Collect consecutive same-color non-space chars */
                var run = '';
                while (c < cols) {
                    var j = (r * cols + c) * 4;
                    var ch2 = grid[j];
                    if (ch2 === 32 || grid[j + 1] !== cr || grid[j + 2] !== cg || grid[j + 3] !== cb) break;
                    var char_str = String.fromCharCode(ch2);
                    if (ch2 === 38) char_str = '&amp;';
                    else if (ch2 === 60) char_str = '&lt;';
                    else if (ch2 === 62) char_str = '&gt;';
                    else if (ch2 === 34) char_str = '&quot;';
                    run += char_str;
                    c++;
                }
                parts.push(run + '</text>');
            }
        }
        parts.push('</svg>');

        var blob = new Blob([parts.join('\n')], { type: 'image/svg+xml' });
        triggerDownload(blob, 'glif-export.svg');
    }

    function doExport() {
        /* C-side export_format not readable from JS; just export PNG by default
           for keyboard shortcut. C-side buttons call glifExport(fmtIdx). */
        exportPNG();
    }

    /* ── File picking (triggered from C via EM_ASM) ── */

    function pickFile(accept, callback) {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = accept;
        input.onchange = function () {
            if (input.files[0]) callback(input.files[0]);
        };
        input.click();
    }

    window.glifPickFont = function () {
        pickFile('.ttf,.otf,.woff', async function (file) {
            const buf = await file.arrayBuffer();
            const bytes = new Uint8Array(buf);
            const ptr = Module._malloc(bytes.length);
            Module.HEAPU8.set(bytes, ptr);
            Module._app_load_font(ptr, bytes.length);
            Module._free(ptr);
        });
    };

    /* ── Drag and drop ── */

    /* Click on drop overlay opens file picker */
    dropOverlay.addEventListener('click', function () {
        pickFile('image/*,video/*,.gif', async function (file) {
            if (file.type.startsWith('video/') || file.type === 'image/gif') {
                startVideoFromFile(file);
            } else if (file.type.startsWith('image/')) {
                stopVideo();
                if (originalImgSrc) URL.revokeObjectURL(originalImgSrc);
                originalImgSrc = URL.createObjectURL(file);
                var img = new Image();
                img.onload = function () {
                    sendImageToWasm(img);
                    showControls(false);
                };
                img.src = originalImgSrc;
            }
        });
    });

    canvasArea.addEventListener('dragover', function (e) { e.preventDefault(); });
    canvasArea.addEventListener('drop', async function (e) {
        e.preventDefault();
        const file = e.dataTransfer.files[0];
        if (!file) return;

        /* Dismiss comparison if active */
        if (compareOn) glifToggleCompare(false);

        if (file.type.startsWith('video/') || file.type === 'image/gif') {
            if (originalImgSrc) { URL.revokeObjectURL(originalImgSrc); originalImgSrc = null; }
            startVideoFromFile(file);
        } else if (file.type.startsWith('image/')) {
            stopVideo();
            /* Keep a URL for comparison */
            if (originalImgSrc) URL.revokeObjectURL(originalImgSrc);
            originalImgSrc = URL.createObjectURL(file);
            const img = new Image();
            img.onload = function () {
                sendImageToWasm(img);
                showControls(false);
            };
            img.src = originalImgSrc;
        } else if (file.name.match(/\.(ttf|otf|woff)$/i)) {
            const buf = await file.arrayBuffer();
            const bytes = new Uint8Array(buf);
            const ptr = Module._malloc(bytes.length);
            Module.HEAPU8.set(bytes, ptr);
            Module._app_load_font(ptr, bytes.length);
            Module._free(ptr);
        }
    });

    function sendImageToWasm(img) {
        const c = document.createElement('canvas');
        c.width = img.naturalWidth || img.width;
        c.height = img.naturalHeight || img.height;
        const ctx = c.getContext('2d');
        ctx.drawImage(img, 0, 0);
        const data = ctx.getImageData(0, 0, c.width, c.height);
        const ptr = Module._malloc(data.data.length);
        Module.HEAPU8.set(data.data, ptr);
        Module._app_load_image(ptr, c.width, c.height, 4);
        Module._free(ptr);
    }

    /* ── Video / webcam ── */

    function stopVideo() {
        var wasCamera = !!videoStream;
        if (videoAnimId) { cancelAnimationFrame(videoAnimId); videoAnimId = null; }
        if (videoStream) { videoStream.getTracks().forEach(function (t) { t.stop(); }); videoStream = null; }
        if (videoEl) { videoEl.pause(); videoEl.remove(); videoEl = null; }
        audioAvailable = false;
        isVideoMode = false;
        /* If webcam was the only content, go back to drop overlay */
        if (wasCamera && !originalImgSrc) {
            hasContent = false;
            if (Module && Module._app_set_content_mode)
                Module._app_set_content_mode(0, 0);
            dropOverlay.classList.remove('hidden');
            requestAnimationFrame(function () { resize(); });
        } else if (hasContent) {
            showControls(false);
        }
    }

    function startVideoFeed(source) {
        stopVideo();
        videoEl = document.createElement('video');
        videoEl.playsInline = true;
        videoEl.style.display = 'none';
        document.body.appendChild(videoEl);

        var isCamera = source instanceof MediaStream;

        if (isCamera) {
            videoEl.srcObject = source;
            videoStream = source;
            videoEl.muted = true;
            audioAvailable = false;
        } else {
            videoEl.src = source;
            videoEl.loop = true;
            videoEl.muted = audioMuted;
            audioAvailable = true;
        }
        videoEl.play();

        if (!isCamera) {
            showControls(true);
        } else {
            showControls(false);
        }

        const offscreen = document.createElement('canvas');
        const ctx = offscreen.getContext('2d', { willReadFrequently: true });

        var mediaTickCount = 0;
        function tick() {
            if (!videoEl || videoEl.readyState < 2) {
                videoAnimId = requestAnimationFrame(tick);
                return;
            }
            const w = videoEl.videoWidth;
            const h = videoEl.videoHeight;
            if (offscreen.width !== w) offscreen.width = w;
            if (offscreen.height !== h) offscreen.height = h;
            ctx.drawImage(videoEl, 0, 0, w, h);
            const frame = ctx.getImageData(0, 0, w, h);
            const ptr = Module._malloc(frame.data.length);
            Module.HEAPU8.set(frame.data, ptr);
            Module._app_video_frame(ptr, w, h);
            if (compareOn) Module._app_upload_video_frame(ptr, w, h);
            Module._free(ptr);

            /* Push media state to C (~10 fps to avoid overhead) */
            if (isVideoMode && Module._app_set_media_state && ++mediaTickCount % 6 === 0) {
                var dur = videoEl.duration || 0;
                if (!isFinite(dur)) dur = 0;
                Module._app_set_media_state(
                    videoEl.paused ? 0 : 1,
                    videoEl.currentTime,
                    dur,
                    audioAvailable ? 1 : 0,
                    audioMuted ? 1 : 0
                );
            }

            videoAnimId = requestAnimationFrame(tick);
        }
        videoAnimId = requestAnimationFrame(tick);
    }

    function startVideoFromFile(file) {
        const url = URL.createObjectURL(file);
        startVideoFeed(url);
    }

    /* ── Camera management ── */

    async function enumerateCameras() {
        try {
            const devices = await navigator.mediaDevices.enumerateDevices();
            cameras = devices.filter(function (d) { return d.kind === 'videoinput'; });
        } catch (e) {
            cameras = [];
        }
    }

    window.glifToggleCamera = async function () {
        if (videoStream) {
            stopVideo();
            return;
        }
        await enumerateCameras();
        currentCamera = 0;
        try {
            const hasKnownId = cameras.length > 0 && cameras[0].deviceId;
            const constraints = hasKnownId
                ? { video: { deviceId: { exact: cameras[0].deviceId } } }
                : { video: { width: 640, height: 480 } };
            const stream = await navigator.mediaDevices.getUserMedia(constraints);
            startVideoFeed(stream);
        } catch (e) {
            console.error('Camera error:', e);
        }
    };

    window.glifSwitchCamera = async function (index) {
        if (index < 0 || index >= cameras.length) return;
        stopVideo();
        currentCamera = index;
        try {
            const stream = await navigator.mediaDevices.getUserMedia({
                video: { deviceId: { exact: cameras[index].deviceId } }
            });
            startVideoFeed(stream);
        } catch (e) {
            console.error('Camera switch error:', e);
        }
    };

    /* ── Frame loop ── */

    function frame() {
        if (Module && Module._app_frame) Module._app_frame();
        requestAnimationFrame(frame);
    }

    /* ── Bootstrap ── */

    createGlifModule({ canvas: canvas }).then(function (mod) {
        Module = mod;
        const dpr = window.devicePixelRatio || 1;
        lastDpr = dpr;
        Module._app_init(dpr, canvas.width, canvas.height);
        resize();
        requestAnimationFrame(frame);
    });
})();
