/**
 * Glif App — Minimal JS bridge for browser APIs.
 *
 * Forwards mouse/touch/keyboard events to the C UI module,
 * handles file picking, webcam, and DPR-aware canvas sizing.
 */
(function () {
    'use strict';

    const canvas = document.getElementById('canvas');

    let Module = null;
    let cameras = [];
    let currentCamera = -1;
    let videoEl = null;
    let videoStream = null;
    let videoAnimId = null;

    /* ── DPR + resize ── */

    function resize() {
        const dpr = window.devicePixelRatio || 1;
        const w = canvas.clientWidth;
        const h = canvas.clientHeight;
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(h * dpr);
        if (Module && Module._app_resize) {
            Module._app_resize(canvas.width, canvas.height);
        }
    }

    window.addEventListener('resize', resize);

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
            let phase = 1; // move
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
        if (Module) Module._app_key(e.keyCode, 1);
    });
    document.addEventListener('keyup', function (e) {
        if (Module) Module._app_key(e.keyCode, 0);
    });

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

    canvas.addEventListener('dragover', function (e) { e.preventDefault(); });
    canvas.addEventListener('drop', async function (e) {
        e.preventDefault();
        const file = e.dataTransfer.files[0];
        if (!file) return;

        if (file.type.startsWith('video/') || file.type === 'image/gif') {
            startVideoFromFile(file);
        } else if (file.type.startsWith('image/')) {
            const img = new Image();
            img.onload = function () { sendImageToWasm(img); URL.revokeObjectURL(img.src); };
            img.src = URL.createObjectURL(file);
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
        if (videoAnimId) { cancelAnimationFrame(videoAnimId); videoAnimId = null; }
        if (videoStream) { videoStream.getTracks().forEach(function (t) { t.stop(); }); videoStream = null; }
        if (videoEl) { videoEl.pause(); videoEl.remove(); videoEl = null; }
    }

    function startVideoFeed(source) {
        stopVideo();
        videoEl = document.createElement('video');
        videoEl.muted = true;
        videoEl.playsInline = true;
        videoEl.style.display = 'none';
        document.body.appendChild(videoEl);

        if (source instanceof MediaStream) {
            videoEl.srcObject = source;
            videoStream = source;
        } else {
            videoEl.src = source;
            videoEl.loop = true;
        }
        videoEl.play();

        const offscreen = document.createElement('canvas');
        const ctx = offscreen.getContext('2d', { willReadFrequently: true });

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
            Module._free(ptr);
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
            const constraints = cameras.length > 0
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
        /* Init before resize — app_resize depends on state set by app_init */
        Module._app_init(dpr, canvas.width, canvas.height);
        resize();
        requestAnimationFrame(frame);
    });
})();
