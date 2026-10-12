// Match CellPad's PS3 button bits; standard browser gamepads use Xbox ordering.
const keys = new Set();
const mapping = {Tab:0, ControlLeft:1, Mouse2:2, Enter:3, Escape:3,
  ArrowUp:4, ArrowRight:5, ArrowDown:6, ArrowLeft:7, KeyQ:8, KeyF:9,
  KeyG:10, Mouse0:11, KeyR:12, KeyX:13, Space:14, KeyE:15};
const padBits = [14,13,15,12,10,11,8,9,0,3,1,2,4,6,7,5];
let mouseX = 0, mouseY = 0, previous = '', mouseCaptured = false;
const canvas = document.querySelector('#screen');
function captureMouse() {
  canvas.focus();
  try {
    canvas.requestPointerLock()?.catch(error => console.warn('Mouse capture unavailable: ' + error.message));
  } catch (error) {
    console.warn('Mouse capture unavailable: ' + error.message);
  }
}
function key(event, held) {
  if (held) keys.add(event.code); else keys.delete(event.code);
  if (event.code in mapping || /^(Key[WASD]|F1)$/.test(event.code)) event.preventDefault();
  if (held && event.code === 'F1') {
    if (document.pointerLockElement) document.exitPointerLock(); else captureMouse();
  }
}
window.addEventListener('keydown', event => key(event, true));
window.addEventListener('keyup', event => key(event, false));
window.addEventListener('blur', () => keys.clear());
canvas.addEventListener('click', captureMouse);
canvas.addEventListener('contextmenu', event => event.preventDefault());
window.addEventListener('mousedown', event => {
  if (document.pointerLockElement === canvas) keys.add('Mouse' + event.button);
});
window.addEventListener('mouseup', event => keys.delete('Mouse' + event.button));
window.addEventListener('mousemove', event => {
  if (document.pointerLockElement === canvas) { mouseX += event.movementX; mouseY += event.movementY; }
});
const axis = value => Math.max(0, Math.min(255, Math.round(128 + value * 127)));
function poll() {
  let buttons = 0;
  for (const [code, bit] of Object.entries(mapping)) if (keys.has(code)) buttons |= 1 << bit;
  const axes = [Number(keys.has('KeyD')) - Number(keys.has('KeyA')),
    Number(keys.has('KeyS')) - Number(keys.has('KeyW')), 0, 0];
  const captured = document.pointerLockElement === canvas;
  if (worker && (mouseX || mouseY || captured !== mouseCaptured)) {
    worker.postMessage({type:'mouse', x:mouseX, y:mouseY, captured});
    mouseCaptured = captured;
  }
  mouseX = mouseY = 0;
  const pad = [...navigator.getGamepads()].find(pad => pad?.mapping === 'standard');
  if (pad) {
    pad.buttons.forEach((button, index) => { if (button.pressed && index < padBits.length) buttons |= 1 << padBits[index]; });
    for (let i = 0; i < 4; ++i) if (Math.abs(pad.axes[i] || 0) > .15) axes[i] = pad.axes[i];
  }
  const packed = axes.map(axis).reduce((value, byte, index) => value | byte << (index * 8), 0) >>> 0;
  const signature = buttons + ':' + packed;
  if (worker && signature !== previous) {
    worker.postMessage({type:'input', buttons, axes:packed});
    previous = signature;
  }
  requestAnimationFrame(poll);
}
requestAnimationFrame(poll);
