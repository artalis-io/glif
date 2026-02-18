/*
 * content-webcam-early.js — Content script at document_start.
 *
 * Runs before page JS loads. If webcam interception was previously
 * enabled, injects webcam.js and activates the handler so the proxy
 * (webcam-proxy.js) intercepts getUserMedia calls.
 */

chrome.storage.local.get(
  ['webcamEnabled', 'dir_crunch', 'global_crunch', 'hires'],
  (data) => {
    if (data.webcamEnabled) {
      chrome.runtime.sendMessage({ action: 'inject-webcam' }, () => {
        // webcam.js is now loaded — activate the handler
        setTimeout(() => {
          document.dispatchEvent(
            new CustomEvent('glif-webcam-start', {
              detail: {
                dir_crunch: data.dir_crunch ?? 1.25,
                global_crunch: data.global_crunch ?? 1.5,
              },
            })
          );
          if (data.hires) {
            document.dispatchEvent(
              new CustomEvent('glif-webcam-hires', {
                detail: { hires: true },
              })
            );
          }
        }, 50);
      });
    }
  }
);
