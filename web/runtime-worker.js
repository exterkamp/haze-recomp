/* Own the Emscripten module in a Worker: WORKERFS uses FileReaderSync to read
   selected multi-gigabyte disc files on demand instead of copying them to RAM. */
let runtime;
let input = {buttons:0, axes:0x80808080};
const send = (type, text) => postMessage({type, text});
function entries(files) {
  return files.map(file => ({name:file.webkitRelativePath.split('/').slice(1).join('/') || file.name, data:file}));
}
function mountAssets(FS, WORKERFS, kind, files, manifest) {
  const root = '/' + kind;
  FS.mkdir(root);
  if (!manifest) {
    FS.mount(WORKERFS, {blobs:entries(files)}, root);
    return;
  }
  for (const path of manifest[kind]) {
    const split = path.lastIndexOf('/');
    const parent = root + (split < 0 ? '' : '/' + path.slice(0, split));
    FS.mkdirTree(parent);
    const node = FS.createLazyFile(parent, path.slice(split + 1),
      '/assets/' + kind + '/' + path.split('/').map(encodeURIComponent).join('/'), true, false);
    // LazyUint8Array's default read calls a getter for every byte. Copy cached
    // HTTP chunks in bulk for the game's large pak reads.
    node.stream_ops.read = (stream, buffer, offset, length, position) => {
      const contents = stream.node.contents;
      const size = Math.max(0, Math.min(length, contents.length - position));
      let copied = 0;
      while (copied < size) {
        const index = position + copied;
        const chunkSize = contents.chunkSize;
        const chunk = contents.getter(Math.floor(index / chunkSize));
        const start = index % chunkSize;
        const count = Math.min(size - copied, chunk.length - start);
        if (count <= 0) throw new Error('Truncated local asset: ' + path);
        buffer.set(chunk.subarray(start, start + count), offset + copied);
        copied += count;
      }
      return size;
    };
  }
}
onmessage = async ({data}) => {
  if (data.type === 'input') {
    input = data;
    runtime?._haze_wasm_input?.(data.buttons, data.axes);
    return;
  }
  if (data.type !== 'start' || runtime) return;
  try {
    const moduleFile = data.smoke === 'gl' ? 'HazeWebGLSmoke.js' : data.smoke ? 'HazeWasmSmoke.js' : 'HazeRecomp.js';
    const {default:createHaze} = await import('/build/' + moduleFile);
    runtime = await createHaze({
      canvas:data.canvas,
      // libc caches environ during initialization, before callMain. Supply the
      // environment before constructors run and before the guest pthread starts.
      preRun:[module => Object.assign(module.ENV, {
        PS3_VFS_ROOT:'/disc', PS3_DEV_FLASH:'/firmware', PS3_SAVEDATA_ROOT:'/savedata',PS3_HDD0_ROOT:'/hdd0',
        PS3_VERBOSE:'0', PS3_HLE_UNRESOLVED:'fail', YDKJ_NO_CRI_CTX:'1',
        PS3_NO_GUEST_PTR_TRAP:'1', HAZE_FRAME_TRACE:'1', AUDIO_MUTE:'1',
      })],
      locateFile: name => '/build/' + name,
      print: text => send('log', text),
      printErr: text => send('log', text),
      onAbort: text => send('error', String(text)),
    });
    const {FS, WORKERFS, IDBFS, ENV} = runtime;
    mountAssets(FS, WORKERFS, 'disc', data.disc, data.assets);
    mountAssets(FS, WORKERFS, 'firmware', data.firmware, data.assets);
    const elf = FS.readdir('/disc/PS3_GAME/USRDIR').find(name => name.toUpperCase() === 'EBOOT.ELF');
    if (!elf) throw new Error('The selected disc directory needs PS3_GAME/USRDIR/EBOOT.ELF (decrypted).');
    FS.mkdir('/savedata');
    FS.mount(IDBFS, {autoPersist:true}, '/savedata');
    FS.mkdir('/hdd0');
    FS.mount(IDBFS, {autoPersist:true}, '/hdd0');
    await new Promise((resolve, reject) => FS.syncfs(true, error => error ? reject(error) : resolve()));
    runtime._haze_wasm_input?.(input.buttons, input.axes);
    if (data.trace)
      setInterval(() => runtime._haze_wasm_report?.(), 15000);
    send('status', 'Booting Haze (browser renderer under development)…');
    runtime.callMain(['/disc/PS3_GAME/USRDIR/' + elf]);
  } catch (error) {
    send('error', error.stack || String(error));
  }
};
