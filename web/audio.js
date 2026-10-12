(() => {
  const button = document.querySelector('#sound');
  let context, node, state, requested = false;
  window.hazeAudioConnect = ({buffer, offset}) => {
    state = new Uint32Array(buffer, offset, 8);
    (async () => {
      context = new AudioContext({sampleRate:48000, latencyHint:'interactive'});
      await context.audioWorklet.addModule('audio-worklet.js');
      node = new AudioWorkletNode(context, 'haze-audio', {
        numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2],
        processorOptions:{buffer, offset},
      });
      node.port.onmessage = ({data}) => { window.hazeAudioStats = data; };
      node.connect(context.destination);
      context.onstatechange = () => {
        Atomics.store(state, 2, context.state === 'running' ? 1 : 0);
        button.textContent = context.state === 'running' ? 'Sound enabled' : 'Enable sound';
      };
      button.disabled = false;
      if (requested) await enable();
    })().catch(error => { button.textContent = 'Audio unavailable'; console.error(error); });
  };
  async function enable() {
    requested = true;
    if (!context || !state) return;
    if (context.state !== 'running') {
      Atomics.store(state, 0, Atomics.load(state, 1));
      await context.resume();
    }
    Atomics.store(state, 2, context.state === 'running' ? 1 : 0);
  }
  button.onclick = enable;
  document.addEventListener('pointerdown', enable);
  document.addEventListener('keydown', enable);
})();
