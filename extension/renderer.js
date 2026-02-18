/*
 * renderer.js — Injected into the page's main world.
 *
 * Injected by the background service worker via chrome.scripting.executeScript
 * with world:"MAIN", which bypasses page CSP. The WASM loader (glif-ext.js)
 * is injected before this script, so createGlifExt is already on window.
 * The extension base URL is set as window.__glifExtBase.
 */

(function () {
  'use strict';

  // Prevent double-injection
  if (window.__glifExtLoaded) return;
  window.__glifExtLoaded = true;

  const EXTENSION_BASE = window.__glifExtBase || '';

  // Capture resolution — no need for full 4K, grid is ~120 columns
  const CAPTURE_W = 480;

  let overlays = []; // { video, canvas, module, offscreen, offscreenCtx, raf, observer }
  let wasmFactory = null;
  let enabled = false;
  let videoObserver = null; // MutationObserver for deferred video detection
  let videoObserverTimeout = null;
  let lastUrl = location.href; // Track URL for SPA navigation

  function emitStatus() {
    document.dispatchEvent(
      new CustomEvent('glif-status', {
        detail: { active: overlays.length > 0, videoCount: overlays.length },
      })
    );
  }

  // Find the best videos on the page
  function findVideos() {
    const videos = document.querySelectorAll('video');
    return Array.from(videos).filter((v) => {
      // Skip tiny or hidden videos (ads, tracking pixels)
      const rect = v.getBoundingClientRect();
      return rect.width > 100 && rect.height > 60;
    });
  }

  function positionOverlay(video, canvas) {
    const videoRect = video.getBoundingClientRect();
    const parentRect = canvas.parentElement.getBoundingClientRect();

    // Position canvas exactly over the video within the parent
    canvas.style.top = (videoRect.top - parentRect.top) + 'px';
    canvas.style.left = (videoRect.left - parentRect.left) + 'px';
    canvas.style.width = videoRect.width + 'px';
    canvas.style.height = videoRect.height + 'px';
    canvas.width = Math.round(videoRect.width * devicePixelRatio);
    canvas.height = Math.round(videoRect.height * devicePixelRatio);
  }

  async function setupOverlay(video) {
    // Skip if already overlaid
    if (video.dataset.glifOverlay) return null;
    video.dataset.glifOverlay = '1';

    const canvas = document.createElement('canvas');
    canvas.style.cssText =
      'position:absolute;top:0;left:0;pointer-events:none;z-index:999999;';

    // Ensure parent is positioned
    const parent = video.parentElement;
    const parentPos = getComputedStyle(parent).position;
    if (parentPos === 'static') {
      parent.style.position = 'relative';
    }
    parent.appendChild(canvas);

    positionOverlay(video, canvas);

    // Temporarily set ID for Emscripten's querySelector-based context creation.
    // Removed after init so multiple overlays don't have duplicate IDs.
    canvas.id = 'glif-ext-canvas';

    let module;
    try {
      module = await wasmFactory({
        canvas: canvas,
        locateFile: (path) => EXTENSION_BASE + 'wasm/' + path,
      });

      const dpr = devicePixelRatio || 1;
      module._ext_init(dpr, canvas.width, canvas.height);
    } catch (err) {
      console.error('[Glif] WASM init failed:', err);
      canvas.removeAttribute('id');
      if (canvas.parentElement) canvas.parentElement.removeChild(canvas);
      delete video.dataset.glifOverlay;
      return null;
    }

    canvas.removeAttribute('id');

    // Offscreen canvas for video frame capture
    const captureH = Math.round(
      CAPTURE_W * (video.videoHeight / (video.videoWidth || 1))
    );
    const offscreen = document.createElement('canvas');
    offscreen.width = CAPTURE_W;
    offscreen.height = captureH || 270;
    const offscreenCtx = offscreen.getContext('2d', {
      willReadFrequently: true,
    });

    const entry = {
      video,
      canvas,
      module,
      offscreen,
      offscreenCtx,
      raf: null,
      observer: null,
      stopped: false,
    };

    // Resize observer — update canvas + WASM when video resizes
    entry.observer = new ResizeObserver(() => {
      positionOverlay(video, canvas);
      module._ext_resize(canvas.width, canvas.height);
      module._ext_render();
    });
    entry.observer.observe(video);

    // Start frame loop
    startFrameLoop(entry);

    return entry;
  }

  function startFrameLoop(entry) {
    const { video, module, offscreen, offscreenCtx } = entry;
    const captureW = offscreen.width;
    const captureH = offscreen.height;

    function onFrame() {
      if (entry.stopped) return;

      // Only process if the video is actually playing
      if (!video.paused && !video.ended && video.readyState >= 2) {
        offscreenCtx.drawImage(video, 0, 0, captureW, captureH);
        const imageData = offscreenCtx.getImageData(0, 0, captureW, captureH);
        const rgba = imageData.data;

        const ptr = module._malloc(rgba.length);
        module.HEAPU8.set(rgba, ptr);
        module._ext_frame(ptr, captureW, captureH);
        module._free(ptr);
      }

      // Use requestVideoFrameCallback if available, otherwise rAF
      if ('requestVideoFrameCallback' in video) {
        entry.raf = video.requestVideoFrameCallback(onFrame);
      } else {
        entry.raf = requestAnimationFrame(onFrame);
      }
    }

    if ('requestVideoFrameCallback' in video) {
      entry.raf = video.requestVideoFrameCallback(onFrame);
    } else {
      entry.raf = requestAnimationFrame(onFrame);
    }
  }

  function destroyOverlay(entry) {
    entry.stopped = true;

    if (entry.observer) {
      entry.observer.disconnect();
      entry.observer = null;
    }

    // Cancel frame callback
    if (entry.raf != null) {
      if ('requestVideoFrameCallback' in entry.video) {
        entry.video.cancelVideoFrameCallback(entry.raf);
      } else {
        cancelAnimationFrame(entry.raf);
      }
      entry.raf = null;
    }

    // Remove overlay canvas
    if (entry.canvas.parentElement) {
      entry.canvas.parentElement.removeChild(entry.canvas);
    }

    delete entry.video.dataset.glifOverlay;
  }

  function destroyAll() {
    enabled = false;
    stopVideoObserver();
    for (const entry of overlays) {
      destroyOverlay(entry);
    }
    overlays = [];
    emitStatus();
  }

  /* ── Deferred video detection via MutationObserver ── */

  function stopVideoObserver() {
    if (videoObserver) {
      videoObserver.disconnect();
      videoObserver = null;
    }
    if (videoObserverTimeout) {
      clearTimeout(videoObserverTimeout);
      videoObserverTimeout = null;
    }
  }

  function startVideoObserver() {
    stopVideoObserver();

    videoObserver = new MutationObserver((mutations) => {
      for (const mutation of mutations) {
        for (const node of mutation.addedNodes) {
          if (node.nodeType !== Node.ELEMENT_NODE) continue;
          const videos = node.tagName === 'VIDEO' ? [node] :
            Array.from(node.querySelectorAll?.('video') || []);

          for (const video of videos) {
            const check = () => {
              if (!enabled) return;
              const rect = video.getBoundingClientRect();
              if (rect.width > 100 && rect.height > 60) {
                setupOverlay(video).then((entry) => {
                  if (entry) {
                    overlays.push(entry);
                    emitStatus();
                    stopVideoObserver();
                  }
                });
              }
            };
            // Video may not have dimensions yet — check on loadedmetadata too
            if (video.readyState >= 1) {
              check();
            } else {
              video.addEventListener('loadedmetadata', check, { once: true });
            }
          }
        }
      }
    });

    videoObserver.observe(document.body, { childList: true, subtree: true });

    // Timeout: stop watching after 30s
    videoObserverTimeout = setTimeout(stopVideoObserver, 30000);
  }

  /* ── SPA navigation hooks ── */

  function installNavigationHooks() {
    const origPushState = history.pushState;
    const origReplaceState = history.replaceState;

    // Only dispatch when the URL actually changes (ignore replaceState
    // calls that just update state without changing the URL, e.g. YouTube
    // updating playback position).
    function checkUrlChanged() {
      const newUrl = location.href;
      if (newUrl !== lastUrl) {
        lastUrl = newUrl;
        window.dispatchEvent(new Event('glif-navigate'));
      }
    }

    history.pushState = function () {
      const result = origPushState.apply(this, arguments);
      checkUrlChanged();
      return result;
    };

    history.replaceState = function () {
      const result = origReplaceState.apply(this, arguments);
      checkUrlChanged();
      return result;
    };

    function onNavigate() {
      if (!enabled) return;
      lastUrl = location.href;

      // Destroy current overlays and re-scan after a short delay
      for (const entry of overlays) {
        destroyOverlay(entry);
      }
      overlays = [];

      setTimeout(() => {
        if (!enabled) return;
        const videos = findVideos();
        if (videos.length > 0) {
          Promise.all(videos.map((v) => setupOverlay(v))).then((entries) => {
            overlays = entries.filter(Boolean);
            emitStatus();
          });
        } else {
          // No videos yet — watch for deferred additions
          startVideoObserver();
        }
      }, 500);
    }

    window.addEventListener('popstate', onNavigate);
    window.addEventListener('glif-navigate', onNavigate);
  }

  async function start() {
    enabled = true;

    // createGlifExt is already on window — injected before this script
    if (!wasmFactory) {
      if (typeof createGlifExt === 'function') {
        wasmFactory = createGlifExt;
      } else {
        console.error('[Glif] createGlifExt not found — WASM loader not injected');
        return;
      }
    }

    const videos = findVideos();
    if (videos.length === 0) {
      console.warn('[Glif] No suitable videos found — watching for additions');
      startVideoObserver();
      emitStatus();
      document.dispatchEvent(new CustomEvent('glif-ready'));
      return;
    }

    for (const video of videos) {
      const entry = await setupOverlay(video);
      if (entry) overlays.push(entry);
    }

    emitStatus();
    document.dispatchEvent(new CustomEvent('glif-ready'));
  }

  // Install SPA navigation hooks immediately
  installNavigationHooks();

  // Listen for events from content.js
  document.addEventListener('glif-start', () => start());
  document.addEventListener('glif-stop', () => destroyAll());

  document.addEventListener('glif-params', (e) => {
    const { dir_crunch, global_crunch } = e.detail;
    for (const entry of overlays) {
      entry.module._ext_set_params(dir_crunch, global_crunch);
      if (entry.video.paused) {
        entry.module._ext_render();
      }
    }
  });

  document.addEventListener('glif-hires', (e) => {
    const hi = e.detail.hires ? 1 : 0;
    for (const entry of overlays) {
      entry.module._ext_set_hires(hi);
    }
  });
})();
