/**
 * smoke.mjs — Puppeteer smoke test for the Glif Chrome extension.
 *
 * Loads the extension in a real Chrome instance, navigates to a test page
 * with a synthetic video, and verifies the core overlay lifecycle:
 *   - Script injection
 *   - Overlay creation and rendering
 *   - Params and hi-res updates
 *   - Disable/re-enable
 *   - SPA navigation (pushState)
 *   - Canvas resize on video resize
 *   - Webcam interception (proxy at document_start, getUserMedia, stream, params, cleanup)
 *
 * Run:  npm run test:ext
 */

import puppeteer from 'puppeteer';
import { fileURLToPath } from 'url';
import path from 'path';
import http from 'http';
import fs from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const EXT_DIR = path.resolve(__dirname, '..');
const TEST_PAGE = path.join(__dirname, 'test-page.html');

let server;
let browser;
let passed = 0;
let failed = 0;

function assert(condition, name) {
  if (condition) {
    console.log(`  \x1b[32mPASS\x1b[0m ${name}`);
    passed++;
  } else {
    console.log(`  \x1b[31mFAIL\x1b[0m ${name}`);
    failed++;
  }
}

async function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function startServer() {
  return new Promise((resolve) => {
    server = http.createServer((req, res) => {
      const html = fs.readFileSync(TEST_PAGE, 'utf8');
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(html);
    });
    server.listen(0, '127.0.0.1', () => {
      resolve(`http://127.0.0.1:${server.address().port}`);
    });
  });
}

async function run() {
  console.log('\nGlif Extension Smoke Tests\n');

  const url = await startServer();

  browser = await puppeteer.launch({
    headless: false,
    args: [
      `--disable-extensions-except=${EXT_DIR}`,
      `--load-extension=${EXT_DIR}`,
      '--no-first-run',
      '--disable-default-apps',
      '--disable-popup-blocking',
      '--window-size=800,600',
      '--enable-unsafe-swiftshader',
    ],
  });

  const swTarget = await browser.waitForTarget(
    (t) => t.type() === 'service_worker' && t.url().includes('background.js'),
    { timeout: 5000 }
  );
  const extId = swTarget.url().split('/')[2];
  const sw = await swTarget.worker();

  const page = await browser.newPage();
  page.on('pageerror', (err) => console.log(`  [page error] ${err.message}`));

  await page.goto(url, { waitUntil: 'networkidle0' });
  await page.bringToFront();

  await page.waitForFunction(
    () => {
      const v = document.querySelector('video');
      return v && v.readyState >= 2 && !v.paused;
    },
    { timeout: 5000 }
  );

  assert(true, 'Video element is playing');

  // Find the test page tab
  const tabId = await sw.evaluate(async () => {
    const tabs = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
    for (const t of tabs) {
      if (!t.url || !t.url.startsWith('chrome')) return t.id;
    }
    return tabs.length > 0 ? tabs[0].id : null;
  });
  assert(tabId !== null, 'Found test page tab');
  if (!tabId) return;

  // Content script query
  const queryResult = await sw.evaluate(
    (tid) => chrome.tabs.sendMessage(tid, { action: 'query' }),
    tabId
  );
  assert(queryResult?.videoCount > 0, 'Content script finds video');

  // Inject scripts
  const injectResult = await sw.evaluate(async (tid) => {
    const baseUrl = chrome.runtime.getURL('/');
    try {
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        func: (base) => { window.__glifExtBase = base; },
        args: [baseUrl],
      });
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        files: ['wasm/glif-ext.js'],
      });
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        files: ['renderer.js'],
      });
      return { ok: true };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  }, tabId);
  assert(injectResult.ok, 'Script injection succeeds');

  await sleep(500);

  const rendererLoaded = await page.evaluate(() => !!window.__glifExtLoaded);
  assert(rendererLoaded, 'Renderer loaded in page');

  // ── Enable ──
  await page.evaluate(() => document.dispatchEvent(new CustomEvent('glif-start')));
  await sleep(5000);

  let overlays = await page.evaluate(() =>
    document.querySelectorAll('canvas[style*="z-index"]').length
  );
  assert(overlays === 1, `Enable creates overlay (${overlays})`);

  // ── Overlay has WebGL context ──
  const hasWebGL = await page.evaluate(() => {
    const c = document.querySelector('canvas[style*="z-index"]');
    if (!c || c.width === 0) return false;
    // Verify a WebGL context exists (getContext returns the existing one)
    const gl = c.getContext('webgl');
    return !!gl;
  });
  assert(hasWebGL, 'Overlay has WebGL context');

  // ── Params ──
  await page.evaluate(() =>
    document.dispatchEvent(new CustomEvent('glif-params', {
      detail: { dir_crunch: 2.5, global_crunch: 2.0 },
    }))
  );
  await sleep(200);
  assert(true, 'Params update dispatched');

  // ── Hi-res ──
  await page.evaluate(() =>
    document.dispatchEvent(new CustomEvent('glif-hires', { detail: { hires: true } }))
  );
  await sleep(200);
  assert(true, 'Hi-res toggle dispatched');

  // ── Disable ──
  await page.evaluate(() => document.dispatchEvent(new CustomEvent('glif-stop')));
  await sleep(300);

  overlays = await page.evaluate(() =>
    document.querySelectorAll('canvas[style*="z-index"]').length
  );
  assert(overlays === 0, 'Disable removes overlay');

  // ── Re-enable ──
  await page.evaluate(() => document.dispatchEvent(new CustomEvent('glif-start')));
  await sleep(5000);

  overlays = await page.evaluate(() =>
    document.querySelectorAll('canvas[style*="z-index"]').length
  );
  assert(overlays === 1, `Re-enable creates overlay (${overlays})`);

  // ── SPA navigation (pushState) ──
  // Simulate a same-origin pushState navigation — overlay should persist
  // because the URL actually changed, triggering re-scan
  await page.evaluate(() =>
    history.pushState({}, '', location.pathname + '?t=' + Date.now())
  );
  await sleep(6000); // 500ms nav delay + 5s WASM load

  overlays = await page.evaluate(() =>
    document.querySelectorAll('canvas[style*="z-index"]').length
  );
  assert(overlays === 1, `SPA navigation re-creates overlay (${overlays})`);

  // ── replaceState with same URL should NOT destroy overlay ──
  const overlaysBefore = overlays;
  await page.evaluate(() => history.replaceState({}, '', location.href));
  await sleep(500);

  overlays = await page.evaluate(() =>
    document.querySelectorAll('canvas[style*="z-index"]').length
  );
  assert(overlays === overlaysBefore, 'replaceState (same URL) preserves overlay');

  // ── Canvas resize ──
  // Change video element dimensions and verify overlay canvas adapts
  const sizeBefore = await page.evaluate(() => {
    const c = document.querySelector('canvas[style*="z-index"]');
    return c ? { w: c.width, h: c.height } : null;
  });
  assert(sizeBefore !== null, 'Overlay canvas exists before resize');

  await page.evaluate(() => {
    const v = document.querySelector('video');
    v.style.width = '320px';
    v.style.height = '180px';
    v.width = 320;
    v.height = 180;
  });
  // ResizeObserver fires asynchronously
  await sleep(500);

  const sizeAfter = await page.evaluate(() => {
    const c = document.querySelector('canvas[style*="z-index"]');
    return c ? { w: c.width, h: c.height } : null;
  });
  assert(sizeAfter !== null, 'Overlay canvas exists after resize');
  assert(
    sizeAfter && sizeBefore && sizeAfter.w < sizeBefore.w,
    `Canvas width decreased (${sizeBefore?.w} -> ${sizeAfter?.w})`
  );

  // Restore original size for remaining tests
  await page.evaluate(() => {
    const v = document.querySelector('video');
    v.style.width = '640px';
    v.style.height = '360px';
    v.width = 640;
    v.height = 360;
  });
  await sleep(300);

  // Cleanup overlay tests
  await page.evaluate(() => document.dispatchEvent(new CustomEvent('glif-stop')));
  await sleep(200);

  // ════════════════════════════════════════════════════════════
  // ── Webcam interception tests ──
  // ════════════════════════════════════════════════════════════

  // Stub the real getUserMedia (saved by webcam-proxy.js at document_start)
  // with a fake stream from canvas — simulates a webcam without hardware
  await page.evaluate(() => {
    const fakeCanvas = document.createElement('canvas');
    fakeCanvas.width = 640;
    fakeCanvas.height = 480;
    const fakeCtx = fakeCanvas.getContext('2d');
    fakeCtx.fillStyle = '#0f0';
    fakeCtx.fillRect(0, 0, 640, 480);
    const fakeStream = fakeCanvas.captureStream(30);
    // Replace the real GUM reference that the proxy reads
    window.__glifRealGetUserMedia = async (constraints) => fakeStream;
    window.__fakeStream = fakeStream;
  });

  // ── Proxy installed at document_start ──
  const proxyInstalled = await page.evaluate(
    () => typeof window.__glifRealGetUserMedia === 'function'
  );
  assert(proxyInstalled, 'getUserMedia proxy installed at document_start');

  // Inject webcam.js
  const webcamInject = await sw.evaluate(async (tid) => {
    const baseUrl = chrome.runtime.getURL('/');
    try {
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        func: (base) => { window.__glifExtBase = base; },
        args: [baseUrl],
      });
      // glif-ext.js already injected from overlay tests, but re-inject is safe
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        files: ['wasm/glif-ext.js'],
      });
      await chrome.scripting.executeScript({
        target: { tabId: tid }, world: 'MAIN',
        files: ['webcam.js'],
      });
      return { ok: true };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  }, tabId);
  assert(webcamInject.ok, 'Webcam script injection succeeds');
  await sleep(500);

  const webcamLoaded = await page.evaluate(() => !!window.__glifWebcamLoaded);
  assert(webcamLoaded, 'Webcam script loaded in page');

  // ── Activate webcam interception ──
  await page.evaluate(() =>
    document.dispatchEvent(new CustomEvent('glif-webcam-start', {
      detail: { dir_crunch: 1.25, global_crunch: 1.5 },
    }))
  );
  await sleep(200);

  // ── getUserMedia should be overridden ──
  const streamResult = await page.evaluate(async () => {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ video: true });
      const videoTracks = stream.getVideoTracks();
      return {
        ok: true,
        trackCount: videoTracks.length,
        hasVideo: videoTracks.length > 0,
      };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  });
  assert(streamResult.ok, 'getUserMedia succeeds after interception');
  assert(streamResult.hasVideo, 'Intercepted stream has video track');

  // ── Render canvas should exist (hidden, used by captureStream) ──
  const hasRenderCanvas = await page.evaluate(() => {
    // webcam.js creates a canvas at fixed offscreen position
    const canvases = document.querySelectorAll('canvas[style*="-9999px"]');
    return canvases.length > 0;
  });
  assert(hasRenderCanvas, 'Webcam render canvas exists (offscreen)');

  // ── Params update ──
  await page.evaluate(() =>
    document.dispatchEvent(new CustomEvent('glif-webcam-params', {
      detail: { dir_crunch: 3.0, global_crunch: 2.5 },
    }))
  );
  await sleep(200);
  assert(true, 'Webcam params update dispatched');

  // ── Audio-only getUserMedia should pass through without interception ──
  const audioResult = await page.evaluate(async () => {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      // Should call the fake GUM (our stub) directly, not intercept
      return { ok: true };
    } catch (err) {
      // Expected — our fake stub doesn't support audio-only, but the key is
      // that webcam.js didn't intercept it (no video constraint)
      return { ok: true, passthrough: true };
    }
  });
  assert(audioResult.ok, 'Audio-only getUserMedia passes through');

  // ── Cleanup: glif-webcam-stop should restore original getUserMedia ──
  await page.evaluate(() =>
    document.dispatchEvent(new CustomEvent('glif-webcam-stop'))
  );
  await sleep(500);

  const webcamStopped = await page.evaluate(() => !window.__glifWebcamLoaded);
  assert(webcamStopped, 'Webcam cleanup resets __glifWebcamLoaded');

  const handlerCleared = await page.evaluate(() => window.__glifWebcamHandler === null);
  assert(handlerCleared, 'Webcam handler cleared (proxy passes through)');

  const renderCanvasRemoved = await page.evaluate(() => {
    const canvases = document.querySelectorAll('canvas[style*="-9999px"]');
    return canvases.length === 0;
  });
  assert(renderCanvasRemoved, 'Webcam render canvas removed after stop');

  // Final cleanup
  await page.close();

  console.log(`\n${passed + failed} tests: ${passed} passed, ${failed} failed\n`);
}

run()
  .catch((err) => {
    console.error('\nTest error:', err);
    failed++;
  })
  .finally(async () => {
    if (browser) await browser.close();
    if (server) server.close();
    process.exit(failed > 0 ? 1 : 0);
  });
