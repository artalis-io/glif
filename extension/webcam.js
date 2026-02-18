/*
 * webcam.js — Injected into the page's MAIN world.
 *
 * Works with webcam-proxy.js (which runs at document_start and installs
 * the getUserMedia proxy). This script sets up the WASM pipeline and
 * registers a handler that the proxy calls when webcam mode is active.
 *
 * Timing: webcam-proxy.js must have already run (it's a manifest content
 * script at document_start). This script is injected later by the
 * background worker: base URL -> glif-ext.js -> webcam.js
 */

(function () {
  'use strict';

  if (window.__glifWebcamLoaded) return;
  window.__glifWebcamLoaded = true;

  const EXTENSION_BASE = window.__glifExtBase || '';
  const STREAM_FPS = 30;

  let wasmModule = null;
  let frameLoopId = null;
  let processedStream = null;
  let realStream = null;
  let renderCanvas = null;
  let captureCanvas = null;
  let captureCtx = null;
  let hiddenVideo = null;
  let captureW = 0;
  let captureH = 0;
  let dirCrunch = 1.25;
  let globalCrunch = 1.5;
  let hiRes = 0;

  async function loadWasm(w, h) {
    if (wasmModule) {
      // Re-init with new dimensions if needed
      if (w !== captureW || h !== captureH) {
        captureW = w;
        captureH = h;
        renderCanvas.width = w;
        renderCanvas.height = h;
        wasmModule._ext_resize(w, h);
      }
      return wasmModule;
    }

    if (typeof createGlifExt !== 'function') {
      console.error('[Glif Webcam] createGlifExt not found');
      return null;
    }

    captureW = w;
    captureH = h;

    // Create a canvas for WebGL rendering — hidden but capturable
    renderCanvas = document.createElement('canvas');
    renderCanvas.id = 'glif-ext-canvas';
    renderCanvas.width = w;
    renderCanvas.height = h;
    renderCanvas.style.cssText =
      'position:fixed;top:-9999px;left:-9999px;width:1px;height:1px;pointer-events:none;';
    document.body.appendChild(renderCanvas);

    wasmModule = await createGlifExt({
      canvas: renderCanvas,
      locateFile: (path) => EXTENSION_BASE + 'wasm/' + path,
    });

    const dpr = 1; // No DPR scaling needed for capture
    wasmModule._ext_init(dpr, w, h);
    renderCanvas.removeAttribute('id');
    wasmModule._ext_set_params(dirCrunch, globalCrunch);
    if (hiRes) wasmModule._ext_set_hires(hiRes);

    return wasmModule;
  }

  function startFrameLoop(videoElement, w, h) {
    captureCanvas = document.createElement('canvas');
    captureCanvas.width = w;
    captureCanvas.height = h;
    captureCtx = captureCanvas.getContext('2d', { willReadFrequently: true });

    function onFrame() {
      if (!window.__glifWebcamHandler) return;

      if (videoElement.readyState >= 2) {
        captureCtx.drawImage(videoElement, 0, 0, w, h);
        const imageData = captureCtx.getImageData(0, 0, w, h);
        const rgba = imageData.data;

        const ptr = wasmModule._malloc(rgba.length);
        wasmModule.HEAPU8.set(rgba, ptr);
        wasmModule._ext_frame(ptr, w, h);
        wasmModule._free(ptr);
      }

      frameLoopId = requestAnimationFrame(onFrame);
    }

    frameLoopId = requestAnimationFrame(onFrame);
  }

  function stopFrameLoop() {
    if (frameLoopId != null) {
      cancelAnimationFrame(frameLoopId);
      frameLoopId = null;
    }
  }

  function cleanup() {
    // Deactivate the proxy handler
    window.__glifWebcamHandler = null;

    stopFrameLoop();

    if (processedStream) {
      processedStream.getTracks().forEach((t) => t.stop());
      processedStream = null;
    }

    if (realStream) {
      realStream.getTracks().forEach((t) => t.stop());
      realStream = null;
    }

    if (hiddenVideo) {
      if (hiddenVideo.parentElement) hiddenVideo.parentElement.removeChild(hiddenVideo);
      hiddenVideo = null;
    }

    if (renderCanvas && renderCanvas.parentElement) {
      renderCanvas.parentElement.removeChild(renderCanvas);
    }
    renderCanvas = null;
    wasmModule = null;
    captureCanvas = null;
    captureCtx = null;

    window.__glifWebcamLoaded = false;
  }

  // Handler called by webcam-proxy.js when getUserMedia is intercepted
  async function handleGetUserMedia(constraints, realGUM) {
    // Get the real webcam stream
    realStream = await realGUM(constraints);

    // Read actual webcam dimensions to preserve aspect ratio
    const videoTrack = realStream.getVideoTracks()[0];
    const settings = videoTrack?.getSettings() || {};
    const srcW = settings.width || 640;
    const srcH = settings.height || 480;

    // Use native resolution — higher res = crisper glyphs in output
    const w = srcW;
    const h = srcH;

    // Load WASM if not already loaded
    const mod = await loadWasm(w, h);
    if (!mod) {
      return realStream; // Fallback to real stream if WASM fails
    }

    // Create a hidden video element to read webcam frames
    hiddenVideo = document.createElement('video');
    hiddenVideo.srcObject = realStream;
    hiddenVideo.muted = true;
    hiddenVideo.playsInline = true;
    hiddenVideo.style.cssText =
      'position:fixed;top:-9999px;left:-9999px;width:1px;height:1px;';
    document.body.appendChild(hiddenVideo);
    await hiddenVideo.play();

    // Start the processing frame loop
    startFrameLoop(hiddenVideo, w, h);

    // Capture the rendered ASCII canvas as a MediaStream
    processedStream = renderCanvas.captureStream(STREAM_FPS);

    // Copy audio tracks from the real stream
    for (const audioTrack of realStream.getAudioTracks()) {
      processedStream.addTrack(audioTrack);
    }

    return processedStream;
  }

  // Listen for control events from content script
  document.addEventListener('glif-webcam-start', (e) => {
    if (e.detail) {
      dirCrunch = e.detail.dir_crunch ?? 1.25;
      globalCrunch = e.detail.global_crunch ?? 1.5;
    }
    if (wasmModule) {
      wasmModule._ext_set_params(dirCrunch, globalCrunch);
    }
    // Register the handler — proxy will start intercepting
    window.__glifWebcamHandler = handleGetUserMedia;
  });

  document.addEventListener('glif-webcam-stop', () => {
    cleanup();
  });

  document.addEventListener('glif-webcam-hires', (e) => {
    hiRes = e.detail.hires ? 1 : 0;
    if (wasmModule) {
      wasmModule._ext_set_hires(hiRes);
    }
  });

  document.addEventListener('glif-webcam-params', (e) => {
    dirCrunch = e.detail.dir_crunch ?? dirCrunch;
    globalCrunch = e.detail.global_crunch ?? globalCrunch;
    if (wasmModule) {
      wasmModule._ext_set_params(dirCrunch, globalCrunch);
    }
  });
})();
