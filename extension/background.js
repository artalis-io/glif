/*
 * background.js — Service worker.
 *
 * Uses chrome.scripting.executeScript with world:"MAIN" to inject scripts
 * into the page context. This is the ONLY mechanism that fully bypasses
 * the page's CSP (including YouTube's Trusted Types policy).
 */

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg.action === 'inject') {
    const tabId = sender.tab.id;
    const baseUrl = chrome.runtime.getURL('/');

    // Sequential injection: base URL → WASM loader → renderer
    chrome.scripting
      .executeScript({
        target: { tabId },
        world: 'MAIN',
        func: (base) => {
          window.__glifExtBase = base;
        },
        args: [baseUrl],
      })
      .then(() =>
        chrome.scripting.executeScript({
          target: { tabId },
          world: 'MAIN',
          files: ['wasm/glif-ext.js'],
        })
      )
      .then(() =>
        chrome.scripting.executeScript({
          target: { tabId },
          world: 'MAIN',
          files: ['renderer.js'],
        })
      )
      .then(() => sendResponse({ ok: true }))
      .catch((err) => {
        console.error('[Glif] Injection failed:', err);
        sendResponse({ ok: false, error: err.message });
      });

    return true; // async sendResponse
  }

  if (msg.action === 'inject-webcam') {
    const tabId = sender.tab.id;
    const baseUrl = chrome.runtime.getURL('/');

    // Sequential injection: base URL → WASM loader → webcam override
    chrome.scripting
      .executeScript({
        target: { tabId },
        world: 'MAIN',
        func: (base) => {
          window.__glifExtBase = base;
        },
        args: [baseUrl],
      })
      .then(() =>
        chrome.scripting.executeScript({
          target: { tabId },
          world: 'MAIN',
          files: ['wasm/glif-ext.js'],
        })
      )
      .then(() =>
        chrome.scripting.executeScript({
          target: { tabId },
          world: 'MAIN',
          files: ['webcam.js'],
        })
      )
      .then(() => sendResponse({ ok: true }))
      .catch((err) => {
        console.error('[Glif] Webcam injection failed:', err);
        sendResponse({ ok: false, error: err.message });
      });

    return true;
  }
});
