const disc = document.querySelector('#disc');
const firmware = document.querySelector('#firmware');
const start = document.querySelector('#start');
const stop = document.querySelector('#stop');
const status = document.querySelector('#status');
const log = document.querySelector('#log');
const query = new URLSearchParams(location.search);
const diagnostics = query.has('debug') || query.has('manual') || query.has('smoke');
document.body.classList.toggle('gameplay', !diagnostics);
let logText = '', logPending = false;
let worker;
function runtimeFailed(text) {
  status.hidden = false;
  status.textContent = 'Runtime failed: ' + text;
  record(text);
  worker?.terminate();
  worker = null;
  stop.disabled = true;
}
function record(text) {
  console.log(text);
  if (!diagnostics) return;
  logText = (logText + text + '\n').slice(-100000);
  if (!logPending) {
    logPending = true;
    setTimeout(() => {
      log.textContent = logText;
      log.scrollTop = log.scrollHeight;
      logPending = false;
    }, 250);
  }
}
if (!diagnostics) document.addEventListener('pointerdown', () => {
  if (document.body.classList.contains('gameplay') && !document.fullscreenElement && document.fullscreenEnabled)
    document.documentElement.requestFullscreen().catch(error => console.warn('Fullscreen unavailable: ' + error.message));
}, {once:true});
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
      if (window.hazeVideoFrames === 1) {
        status.textContent = 'Haze is running.';
        status.hidden = !diagnostics;
      }
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
    glValidate:new URLSearchParams(location.search).has('glvalidate'),
    smoke:new URLSearchParams(location.search).get('smoke')}, [canvas]);
  stop.disabled = false;
}
start.onclick = () => launch(null);
stop.onclick = () => {
  // Reload also terminates all pthread workers owned by the module.
  location.search = '?manual=1';
};
function chooseAssets(message) {
  document.body.classList.remove('gameplay');
  status.hidden = false;
  status.textContent = message;
}
if (supported && !query.has('manual')) {
  fetch('/assets/manifest.json').then(response => response.json()).then(assets => {
    if (assets.disc?.some(path => path.toUpperCase() === 'PS3_GAME/USRDIR/EBOOT.ELF')) {
      document.querySelector('#assets').hidden = true;
      status.textContent = 'Starting with local game assets...';
      launch(assets);
    } else chooseAssets('Choose your local game assets to start.');
  }).catch(error => chooseAssets('Local assets unavailable: ' + error.message));
}
