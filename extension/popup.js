const toggleBtn = document.getElementById('toggle');
const webcamBtn = document.getElementById('webcam-toggle');
const hiresBtn = document.getElementById('hires');
const dirSlider = document.getElementById('dir-crunch');
const globalSlider = document.getElementById('global-crunch');
const dirVal = document.getElementById('dir-val');
const globalVal = document.getElementById('global-val');
const statusEl = document.getElementById('status');

let active = false;
let hires = false;
let webcamActive = false;

function getTab() {
  return chrome.tabs.query({ active: true, currentWindow: true }).then((tabs) => tabs[0]);
}

function sendMessage(tab, msg) {
  return chrome.tabs.sendMessage(tab.id, msg);
}

function updateUI() {
  toggleBtn.textContent = active ? 'Disable' : 'Enable';
  toggleBtn.classList.toggle('active', active);
  hiresBtn.textContent = hires ? 'Hi Res' : 'Lo Res';
  hiresBtn.classList.toggle('active', hires);
  webcamBtn.textContent = webcamActive ? 'Webcam Off' : 'Webcam';
  webcamBtn.classList.toggle('active', webcamActive);
}

function getParams() {
  return {
    dir_crunch: parseFloat(dirSlider.value),
    global_crunch: parseFloat(globalSlider.value),
  };
}

function sendParams(tab) {
  const params = getParams();
  sendMessage(tab, { action: 'set-params', ...params });
  if (webcamActive) {
    sendMessage(tab, { action: 'webcam-params', ...params });
  }
}

// Toggle button
toggleBtn.addEventListener('click', async () => {
  const tab = await getTab();
  if (!tab) return;

  active = !active;
  updateUI();

  if (active) {
    await sendMessage(tab, { action: 'enable' });
    statusEl.textContent = 'Starting...';
  } else {
    await sendMessage(tab, { action: 'disable' });
    statusEl.textContent = 'Disabled';
  }
});

// Webcam toggle
webcamBtn.addEventListener('click', async () => {
  const tab = await getTab();
  if (!tab) return;

  webcamActive = !webcamActive;
  chrome.storage.local.set({ webcamEnabled: webcamActive });
  updateUI();

  if (webcamActive) {
    const params = getParams();
    await sendMessage(tab, { action: 'webcam-enable', ...params });
    if (hires) {
      sendMessage(tab, { action: 'webcam-hires', hires: true });
    }
    statusEl.textContent = 'Webcam intercepting — rejoin call to apply';
  } else {
    await sendMessage(tab, { action: 'webcam-disable' });
    statusEl.textContent = 'Webcam disabled';
  }
});

// Hi-res toggle
hiresBtn.addEventListener('click', async () => {
  hires = !hires;
  updateUI();
  chrome.storage.local.set({ hires });
  const tab = await getTab();
  if (tab && active) {
    sendMessage(tab, { action: 'set-hires', hires });
  }
  if (tab && webcamActive) {
    sendMessage(tab, { action: 'webcam-hires', hires });
  }
});

// Sliders
dirSlider.addEventListener('input', async () => {
  const val = parseFloat(dirSlider.value);
  dirVal.textContent = val.toFixed(2);
  chrome.storage.local.set({ dir_crunch: val });
  const tab = await getTab();
  if (tab && (active || webcamActive)) sendParams(tab);
});

globalSlider.addEventListener('input', async () => {
  const val = parseFloat(globalSlider.value);
  globalVal.textContent = val.toFixed(2);
  chrome.storage.local.set({ global_crunch: val });
  const tab = await getTab();
  if (tab && (active || webcamActive)) sendParams(tab);
});

// Listen for status updates from content script
chrome.runtime.onMessage.addListener((msg) => {
  if (msg.action === 'status') {
    active = msg.active;
    updateUI();
    if (msg.active) {
      statusEl.textContent = `Active on ${msg.videoCount} video${msg.videoCount !== 1 ? 's' : ''}`;
    }
  }
});

// Read persisted state + query current tab
(async () => {
  const data = await chrome.storage.local.get([
    'enabled', 'dir_crunch', 'global_crunch', 'hires', 'webcamEnabled',
  ]);

  if (data.dir_crunch != null) {
    dirSlider.value = data.dir_crunch;
    dirVal.textContent = parseFloat(data.dir_crunch).toFixed(2);
  }
  if (data.global_crunch != null) {
    globalSlider.value = data.global_crunch;
    globalVal.textContent = parseFloat(data.global_crunch).toFixed(2);
  }
  hires = !!data.hires;
  active = !!data.enabled;
  webcamActive = !!data.webcamEnabled;
  updateUI();

  const tab = await getTab();
  if (!tab) return;
  try {
    const resp = await sendMessage(tab, { action: 'query' });
    if (resp && resp.videoCount > 0) {
      statusEl.textContent = active
        ? `Active on ${resp.videoCount} video${resp.videoCount !== 1 ? 's' : ''}`
        : `${resp.videoCount} video${resp.videoCount !== 1 ? 's' : ''} found`;
    } else {
      statusEl.textContent = active ? 'Enabled — no videos on this page' : 'No videos found on this page';
    }
  } catch {
    statusEl.textContent = 'Navigate to a page with video';
  }
})();
