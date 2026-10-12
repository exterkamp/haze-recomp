// The DOM canvas has already been transferred to the runtime Worker. Register
// that OffscreenCanvas so Emscripten can transfer it again to the render pthread.
Module['preRun'] ??= [];
Module['preRun'].push(() => {
  const canvas = Module['canvas'];
  canvas.id = 'canvas';
  const shared = _malloc(16);
  HEAP32[Number(shared) / 4] = canvas.width;
  HEAP32[Number(shared) / 4 + 1] = canvas.height;
  HEAPU64[Number(shared) / 8 + 1] = 0n;
  GL.offscreenCanvases.canvas = {offscreenCanvas:canvas, canvasSharedPtr:shared, id:'canvas'};
});
