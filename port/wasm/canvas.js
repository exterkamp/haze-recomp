// Register the runtime's OffscreenCanvas for transfer to the render pthread.
// Send completed ImageBitmaps back to the page explicitly: a DOM placeholder
// does not reliably update through the runtime's multiple worker transfers.
addEventListener('error', event => {
  if (event.error?.stack) postMessage({type:'haze-worker-error', text:event.error.stack});
});
Module['preRun'] ??= [];
// Pthread workers initialize without running the owner's preRun hooks.
globalThis.hazeGetGL = () => typeof GLctx === 'undefined' ? null : GLctx;
Module['preRun'].push(() => {
  // This hook allocates the canvas handoff record before initRuntime. Set
  // Binaryen's guard bounds before the first instrumented malloc call.
  if (typeof setStackLimits === 'function') setStackLimits();
  if (!ENVIRONMENT_IS_PTHREAD) {
    const forward = worker => worker.addEventListener('message', event => {
      if (event.data.type === 'haze-worker-error') {
        event.stopImmediatePropagation();
        postMessage({type:'error', text:event.data.text});
      }
      if (event.data.type === 'haze-frame') {
        event.stopImmediatePropagation();
        const frame = event.data.frame;
        postMessage({type:'frame', frame}, [frame]);
      }
    }, true);
    PThread.unusedWorkers.forEach(forward);
    const allocate = PThread.allocateUnusedWorker;
    PThread.allocateUnusedWorker = () => {
      const worker = allocate();
      forward(worker);
      return worker;
    };
  }
  const canvas = Module['canvas'];
  canvas.id = 'canvas';
  const shared = _malloc(16);
  HEAP32[Number(shared) / 4] = canvas.width;
  HEAP32[Number(shared) / 4 + 1] = canvas.height;
  HEAPU64[Number(shared) / 8 + 1] = 0n;
  GL.offscreenCanvases.canvas = {offscreenCanvas:canvas, canvasSharedPtr:shared, id:'canvas'};
});
