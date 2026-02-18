/*
 * webcam-proxy.js — MAIN world content script at document_start.
 *
 * Installs a transparent getUserMedia proxy before ANY page JS executes.
 * When webcam interception is inactive, calls pass through to the real
 * getUserMedia with zero overhead. When active, delegates to the handler
 * set by webcam.js (injected later via background worker).
 *
 * This solves the Google Meet / Teams timing issue: these apps cache a
 * reference to getUserMedia during module initialization. By installing
 * the proxy at document_start, even cached references point to our wrapper.
 */

(function () {
  'use strict';

  if (!navigator.mediaDevices?.getUserMedia) return;

  const realGUM = navigator.mediaDevices.getUserMedia.bind(
    navigator.mediaDevices
  );

  // Expose so webcam.js and tests can access/replace it
  window.__glifRealGetUserMedia = realGUM;

  navigator.mediaDevices.getUserMedia = function (constraints) {
    // Read from window each time so webcam.js can set/clear the handler
    const handler = window.__glifWebcamHandler;
    if (handler && constraints?.video) {
      return handler(constraints, window.__glifRealGetUserMedia);
    }
    return window.__glifRealGetUserMedia.call(
      navigator.mediaDevices,
      constraints
    );
  };
})();
