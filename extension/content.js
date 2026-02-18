/*
 * content.js — Content script (Chrome isolated world).
 *
 * Relays messages between the popup and the page's main world.
 * Script injection is handled by the background service worker using
 * chrome.scripting.executeScript (bypasses page CSP).
 *
 * Persists enabled state via chrome.storage.local — if previously enabled,
 * auto-injects and starts on page load.
 */

let injected = false;
let webcamInjected = false;

function findVideos() {
  const videos = document.querySelectorAll('video');
  return Array.from(videos).filter(
    (v) => v.readyState >= 1 || v.src || v.querySelector('source')
  );
}

function sendToPage(type, detail) {
  document.dispatchEvent(
    new CustomEvent(type, { detail: detail || {} })
  );
}

function sendSavedParams() {
  chrome.storage.local.get(['dir_crunch', 'global_crunch', 'hires'], (data) => {
    sendToPage('glif-params', {
      dir_crunch: data.dir_crunch ?? 1.25,
      global_crunch: data.global_crunch ?? 1.5,
    });
    if (data.hires) {
      sendToPage('glif-hires', { hires: true });
    }
  });
}

function injectAndStart() {
  if (injected) {
    sendToPage('glif-start');
    return;
  }
  injected = true;
  chrome.runtime.sendMessage({ action: 'inject' }, (resp) => {
    if (resp?.ok) {
      setTimeout(() => sendToPage('glif-start'), 50);
    } else {
      console.error('[Glif] Injection failed:', resp?.error);
      injected = false;
    }
  });
}

// When renderer.js signals ready, send saved params
document.addEventListener('glif-ready', () => {
  sendSavedParams();
});

// Listen for status updates from renderer.js (page world -> content script)
document.addEventListener('glif-status', (e) => {
  chrome.runtime.sendMessage({
    action: 'status',
    active: e.detail.active,
    videoCount: e.detail.videoCount,
  });
});

// Listen for messages from popup
chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg.action === 'enable') {
    chrome.storage.local.set({ enabled: true });
    injectAndStart();
    sendResponse({ ok: true });
  } else if (msg.action === 'disable') {
    chrome.storage.local.set({ enabled: false });
    sendToPage('glif-stop');
    injected = false;
    sendResponse({ ok: true });
  } else if (msg.action === 'set-params') {
    sendToPage('glif-params', {
      dir_crunch: msg.dir_crunch,
      global_crunch: msg.global_crunch,
    });
    sendResponse({ ok: true });
  } else if (msg.action === 'set-hires') {
    sendToPage('glif-hires', { hires: msg.hires });
    sendResponse({ ok: true });
  } else if (msg.action === 'query') {
    const videos = findVideos();
    sendResponse({ videoCount: videos.length, active: injected });
  } else if (msg.action === 'webcam-enable') {
    const params = {
      dir_crunch: msg.dir_crunch,
      global_crunch: msg.global_crunch,
    };
    if (webcamInjected) {
      sendToPage('glif-webcam-start', params);
      sendResponse({ ok: true });
    } else {
      chrome.runtime.sendMessage({ action: 'inject-webcam' }, (resp) => {
        if (resp?.ok) {
          webcamInjected = true;
          setTimeout(() => sendToPage('glif-webcam-start', params), 50);
        }
        sendResponse({ ok: true });
      });
    }
    return true; // async sendResponse
  } else if (msg.action === 'webcam-disable') {
    sendToPage('glif-webcam-stop');
    webcamInjected = false;
    sendResponse({ ok: true });
  } else if (msg.action === 'webcam-hires') {
    sendToPage('glif-webcam-hires', { hires: msg.hires });
    sendResponse({ ok: true });
  } else if (msg.action === 'webcam-params') {
    sendToPage('glif-webcam-params', {
      dir_crunch: msg.dir_crunch,
      global_crunch: msg.global_crunch,
    });
    sendResponse({ ok: true });
  }
  return true;
});

// Auto-start on page load if previously enabled
chrome.storage.local.get(['enabled'], (data) => {
  if (data.enabled) {
    injectAndStart();
  }
});
