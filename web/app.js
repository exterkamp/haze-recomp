const disc = document.querySelector('#disc');
const firmware = document.querySelector('#firmware');
const start = document.querySelector('#start');
const stop = document.querySelector('#stop');
const status = document.querySelector('#status');
const log = document.querySelector('#log');
let worker;
function runtimeFailed(text) {
  status.textContent = 'Runtime failed: ' + text;
  record(text);
  worker?.terminate();
  worker = null;
  stop.disabled = true;
}
function record(text) {
  console.log(text);
  log.textContent = (log.textContent + text + '\n').slice(-100000);
  log.scrollTop = log.scrollHeight;
}
const supported = crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined' && typeof Worker !== 'undefined';
status.textContent = supported ? 'Select the disc and firmware directories.' : 'Shared memory is unavailable. Run scripts/serve-wasm.py and open its localhost URL.';
disc.onchange = () => { start.disabled = !supported || !disc.files.length || !!worker; };
function launch(assets) {
  if ((!assets && !disc.files.length) || worker) return;
  start.disabled = true;
  status.textContent = 'Loading WebAssembly…';
  worker = new Worker('runtime-worker.js');
  const screen = document.querySelector('#screen');
  const display = screen.getContext('bitmaprenderer');
  worker.onmessage = ({data}) => {
    if (data.type === 'frame') {
      display.transferFromImageBitmap(data.frame);
      window.hazeVideoFrames = (window.hazeVideoFrames || 0) + 1;
    }
    else if (data.type === 'audio') window.hazeAudioConnect(data);
    else if (data.type === 'status') status.textContent = data.text;
    else if (data.type === 'log') record(data.text);
    else if (data.type === 'error') runtimeFailed(data.text);
  };
  worker.onerror = event => runtimeFailed(event.message);
  const canvas = new OffscreenCanvas(screen.width, screen.height);
  worker.postMessage({type:'start', disc:[...disc.files], firmware:[...firmware.files], assets, canvas,
    trace:new URLSearchParams(location.search).has('trace'),
    smoke:new URLSearchParams(location.search).get('smoke')}, [canvas]);
  stop.disabled = false;
}
start.onclick = () => launch(null);
stop.onclick = () => {
  // Reload also terminates all pthread workers owned by the module.
  location.search = '?manual=1';
};
if (supported && !new URLSearchParams(location.search).has('manual')) {
  fetch('/assets/manifest.json').then(response => response.json()).then(assets => {
    if (assets.disc?.some(path => path.toUpperCase() === 'PS3_GAME/USRDIR/EBOOT.ELF')) {
      document.querySelector('#assets').hidden = true;
      status.textContent = 'Starting with local game assets...';
      launch(assets);
    }
  }).catch(error => record('Local assets unavailable: ' + error.message));
}
