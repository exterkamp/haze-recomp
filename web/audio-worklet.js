class HazeAudio extends AudioWorkletProcessor {
  constructor({processorOptions:{buffer, offset}}) {
    super();
    this.state = new Uint32Array(buffer, offset, 8);
    this.capacity = this.state[3];
    this.samples = new Float32Array(buffer, offset + 64, this.capacity * 2);
    this.ticks = 0;
    this.peak = 0;
  }
  process(inputs, outputs) {
    const [left, right] = outputs[0];
    if (!Atomics.load(this.state, 2)) return true;
    const read = Atomics.load(this.state, 0);
    const write = Atomics.load(this.state, 1);
    const count = Math.min((write - read) >>> 0, left.length);
    for (let i = 0; i < count; ++i) {
      const slot = ((read + i) % this.capacity) * 2;
      const l = this.samples[slot], r = this.samples[slot + 1];
      left[i] = Number.isFinite(l) ? Math.max(-1, Math.min(1, l)) : 0;
      right[i] = Number.isFinite(r) ? Math.max(-1, Math.min(1, r)) : 0;
      this.peak = Math.max(this.peak, Math.abs(left[i]), Math.abs(right[i]));
    }
    Atomics.store(this.state, 0, (read + count) >>> 0);
    Atomics.add(this.state, 4, count);
    if (count < left.length) Atomics.add(this.state, 5, left.length - count);
    if (++this.ticks >= 375) {
      this.port.postMessage({peak:this.peak, rendered:Atomics.load(this.state,4),
        underruns:Atomics.load(this.state,5), dropped:Atomics.load(this.state,6),
        submitted:Atomics.load(this.state,7)});
      this.ticks = 0;
      this.peak = 0;
    }
    return true;
  }
}
registerProcessor('haze-audio', HazeAudio);
