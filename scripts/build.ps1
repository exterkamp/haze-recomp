param(
    [switch]$RuntimeOnly,
    [switch]$Test,
    [switch]$ProfileRenderer,
    [switch]$SpuFma,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# Import the compiler/SDK environment into this process. No filesystem
# operations are delegated to cmd.exe.
$environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual Studio environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
$python = Join-Path $root '.venv\Scripts\python.exe'
$sdk = Join-Path $root 'external\ps3recomp'
if ($RuntimeOnly) {
    $source = $sdk
    $build = Join-Path $root 'build\runtime'
} else {
    if (-not (Test-Path -LiteralPath 'generated/ppu/ppu_recomp.h')) {
        throw 'No Haze lift exists. Run .venv\Scripts\python.exe scripts/haze.py lift first.'
    }
    $source = Join-Path $root 'port'
    $build = Join-Path $root 'build\haze'
}
$ErrorActionPreference = 'Continue' # Windows PowerShell treats native stderr warnings as errors.
& $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$clang" "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_BUILD_TYPE=$Configuration" "-DPython3_EXECUTABLE=$python" "-DPS3RECOMP_DIR=$sdk" "-DRECOMP_DIR=$root/generated/ppu" "-DHAZE_RSX_PROFILE=$([int][bool]$ProfileRenderer)" "-DHAZE_SPU_FMA=$([int][bool]$SpuFma)"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($Test) {
    $coherencyTest = Join-Path $root 'local/test_coherency_lifetime.exe'
    & $clang /nologo /W0 /O2 /clang:-msse4.1 "/I$sdk/include" "/Fe$coherencyTest" "/Fo$root/local/" "$sdk/runtime/spu/tests/test_coherency_lifetime.c" "$sdk/runtime/spu/spu_coherency.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU coherency lifetime test compilation failed.' }
    & $coherencyTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU coherency lifetime/holder-capacity test failed.' }
    $abiStorageTest = Join-Path $root 'local/test_abi_storage.exe'
    & $clang /nologo /W0 /O2 /clang:-msse4.1 "/I$sdk/include" "/Fe$abiStorageTest" "/Fo$root/local/test_abi_storage.obj" "$sdk/runtime/spu/tests/test_abi_storage.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU ABI storage test compilation failed.' }
    & $abiStorageTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU ABI storage semantics test failed.' }
    $depthTest = Join-Path $root 'local/test_depth_history.exe'
    & $clang /nologo /W0 /O2 /Gy /Gw "/I$sdk/include" "/Fe$depthTest" "/Fo$root/local/test_depth_history.obj" "$sdk/runtime/spu/tests/test_depth_history.c" /link /OPT:REF
    if ($LASTEXITCODE -ne 0) { throw 'SPU private-history test compilation failed.' }
    & $depthTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU history isolation/depth-limit test failed.' }
    $sleepTest = Join-Path $root 'local/test_usleep_timer.exe'
    & $clang /nologo /W0 /O2 "/Fe$sleepTest" "/Fo$root/local/test_usleep_timer.obj" "$sdk/runtime/syscalls/tests/test_usleep_timer.c"
    if ($LASTEXITCODE -ne 0) { throw 'High-resolution timer test compilation failed.' }
    & $sleepTest
    if ($LASTEXITCODE -ne 0) { throw 'High-resolution timer duration/handle test failed.' }
    $fingerprintTest = Join-Path $root 'local/test_fingerprint_cache.exe'
    & $clang /nologo /W0 /O2 "/I$sdk/include" "/Fe$fingerprintTest" "/Fo$root/local/test_fingerprint_cache.obj" "$sdk/runtime/spu/tests/test_fingerprint_cache.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU fingerprint cache test compilation failed.' }
    & $fingerprintTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU fingerprint cache identity/concurrency test failed.' }
    foreach ($primitive in @('test_spu_helpers', 'test_spu_shufb')) {
        $primitiveTest = Join-Path $root "local/$primitive.exe"
        & $clang /nologo /W0 /O1 /clang:-msse4.1 "/I$sdk/include" "/Fe$primitiveTest" "/Fo$root/local/$primitive.obj" "$sdk/runtime/spu/tests/$primitive.c"
        if ($LASTEXITCODE -ne 0) { throw "SPU SIMD test compilation failed: $primitive" }
        & $primitiveTest
        if ($LASTEXITCODE -ne 0) { throw "SPU SIMD execution failed: $primitive" }
    }
    $fmaTest = Join-Path $root 'local/test_spu_fma.exe'
    $fmaFlags = $(if ($SpuFma) { @('/clang:-mavx', '/clang:-mfma') } else { @() })
    & $clang /nologo /W0 /O2 /clang:-msse4.1 @fmaFlags "/I$sdk/include" "/Fe$fmaTest" "/Fo$root/local/test_spu_fma.obj" "$root/tests/test_spu_fma.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU fused arithmetic test compilation failed.' }
    & $fmaTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU fused arithmetic differs from scalar reference.' }
    $frameTest = Join-Path $root 'local/test_frame_pump.exe'
    & $clang /nologo /W0 /O1 /std:c++20 "/Fe$frameTest" "/Fo$root/local/test_frame_pump.obj" "$root/tests/test_frame_pump.cpp"
    if ($LASTEXITCODE -ne 0) { throw 'Frame-pump test compilation failed.' }
    & $frameTest
    if ($LASTEXITCODE -ne 0) { throw 'Frame-boundary presentation test failed.' }
    & $python "$root/tests/test_firmware_relocation.py"
    if ($LASTEXITCODE -ne 0) { throw 'Firmware ELF relocation tests failed.' }
    $audioOutTest = Join-Path $root 'local/test_audio_out_abi.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$audioOutTest" "/Fo$root/local/test_audio_out_abi.obj" "$sdk/libs/misc/tests/test_audio_out_abi.c"
    if ($LASTEXITCODE -ne 0) { throw 'Audio-output ABI test compilation failed.' }
    & $audioOutTest
    if ($LASTEXITCODE -ne 0) { throw 'Audio-output device/state ABI test failed.' }
    $padTest = Join-Path $root 'local/test_host_input.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$padTest" "/Fo$root/local/test_host_input.obj" "$sdk/libs/input/tests/test_host_input.c" /link user32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Host pad input test compilation failed.' }
    & $padTest
    if ($LASTEXITCODE -ne 0) { throw 'Host pad input packet test failed.' }
    $hazeInputTest = Join-Path $root 'local/test_haze_input.exe'
    & $clang /nologo /W0 /O1 /std:c++20 "/I$sdk/include" "/I$sdk/libs/input" "/Fe$hazeInputTest" "/Fo$root/local/test_haze_input.obj" "$root/tests/test_haze_input.cpp" /link user32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Haze keyboard/mouse test compilation failed.' }
    & $hazeInputTest
    if ($LASTEXITCODE -ne 0) { throw 'Haze keyboard/mouse mapping test failed.' }
    & $python "$sdk/tools/test_spu_text_selection.py"
    if ($LASTEXITCODE -ne 0) { throw 'SPU segment-selection regression tests failed.' }
    & $python "$sdk/tools/test_spu_trace_registers.py"
    if ($LASTEXITCODE -ne 0) { throw 'SPU destination-register trace tests failed.' }
    $oldCC = $env:CC
    try {
        $env:CC = Join-Path (Split-Path $clang -Parent) 'clang.exe'
        foreach ($vectorTest in @('test_ppu_vsum4ubs.py', 'test_ppu_vsl.py')) {
            & $python "$sdk/tools/$vectorTest"
            if ($LASTEXITCODE -ne 0) { throw "PPU vector regression failed: $vectorTest" }
        }
    } finally { $env:CC = $oldCC }
    $condTest = Join-Path $root 'local/test_sysprx_lwcond.exe'
    & $clang /nologo /W0 /O1 /Gy /Gw "/I$root/generated/ppu" "/I$sdk/include" "/Fe$condTest" "/Fo$root/local/test_sysprx_lwcond.obj" "$sdk/runtime/ppu/tests/test_sysprx_lwcond.cpp" /link /OPT:REF
    if ($LASTEXITCODE -ne 0) { throw 'Lightweight condition-variable test compilation failed.' }
    & $condTest
    if ($LASTEXITCODE -ne 0) { throw 'Lightweight condition-variable boundary and wait test failed.' }
    $queueTest = Join-Path $root 'local/test_queue_size.exe'
    New-Item -ItemType Directory -Path (Split-Path $queueTest -Parent) -Force | Out-Null
    & $clang /nologo /W0 "/I$sdk/include" "/I$sdk/runtime/spu" "/Fe$queueTest" "/Fo$root/local/test_queue_size.obj" "$sdk/libs/spurs/tests/test_queue_size.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPURS queue query test compilation failed.' }
    & $queueTest
    if ($LASTEXITCODE -ne 0) { throw 'SPURS queue query contract test failed.' }
    $gameDataTest = Join-Path $root 'local/test_gamedata_callback.exe'
    & $clang /nologo /W0 "/I$sdk/include" "/Fe$gameDataTest" "/Fo$root/local/test_gamedata_callback.obj" "$sdk/libs/system/tests/test_gamedata_callback.c"
    if ($LASTEXITCODE -ne 0) { throw 'Game-data callback test compilation failed.' }
    & $gameDataTest
    if ($LASTEXITCODE -ne 0) { throw 'Game-data callback ABI test failed.' }
    $dmaTest = Join-Path $root 'local/test_dma_repeat.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/I$sdk/runtime/platform" "/Fe$dmaTest" "/Fo$root/local/test_dma_repeat.obj" "$sdk/runtime/spu/tests/test_dma_repeat.c"
    if ($LASTEXITCODE -ne 0) { throw 'DMA repeat-watchdog test compilation failed.' }
    & $dmaTest
    if ($LASTEXITCODE -ne 0) { throw 'DMA repeat-watchdog contract test failed.' }
    $mfcTest = Join-Path $root 'local/test_mfc_context_state.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$mfcTest" "/Fo$root/local/test_mfc_context_state.obj" "$sdk/runtime/spu/tests/test_mfc_context_state.c"
    if ($LASTEXITCODE -ne 0) { throw 'Private MFC state test compilation failed.' }
    & $mfcTest
    if ($LASTEXITCODE -ne 0) { throw 'Private MFC context isolation test failed.' }
    $dispatchTest = Join-Path $root 'local/test_taskset_dispatch.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/I$sdk/runtime/platform" "/Fe$dispatchTest" "/Fo$root/local/test_taskset_dispatch.obj" "$sdk/runtime/spu/tests/test_taskset_dispatch.c"
    if ($LASTEXITCODE -ne 0) { throw 'Taskset dispatch test compilation failed.' }
    & $dispatchTest
    if ($LASTEXITCODE -ne 0) { throw 'Taskset dispatch contract test failed.' }
    $publicationTest = Join-Path $root 'local/test_taskset_publication.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$publicationTest" "/Fo$root/local/test_taskset_publication.obj" "$sdk/runtime/spu/tests/test_taskset_publication.c"
    if ($LASTEXITCODE -ne 0) { throw 'Concurrent task publication test compilation failed.' }
    $oldVerbose = $env:PS3_VERBOSE
    try {
        $env:PS3_VERBOSE = '0'
        & $publicationTest
        if ($LASTEXITCODE -ne 0) { throw 'Concurrent task publication and recycling test failed.' }
    } finally { $env:PS3_VERBOSE = $oldVerbose }
    $drawBatchTest = Join-Path $root 'local/test_draw_batches.exe'
    & $clang /nologo /W0 /O1 "/Fe$drawBatchTest" "/Fo$root/local/test_draw_batches.obj" "$sdk/libs/video/tests/test_draw_batches.c"
    if ($LASTEXITCODE -ne 0) { throw 'Draw-packet storage test compilation failed.' }
    & $drawBatchTest
    if ($LASTEXITCODE -ne 0) { throw 'Draw-packet growth and order test failed.' }
    $m2mfTest = Join-Path $root 'local/test_m2mf.exe'
    & $clang /nologo /W0 /O1 "/Fe$m2mfTest" "/Fo$root/local/test_m2mf.obj" "$sdk/libs/video/tests/test_m2mf.c"
    if ($LASTEXITCODE -ne 0) { throw 'GPU buffer-copy test compilation failed.' }
    & $m2mfTest
    if ($LASTEXITCODE -ne 0) { throw 'GPU buffer-copy contract test failed.' }
    $fifoTest = Join-Path $root 'local/test_fifo_recycle.exe'
    & $clang /nologo /W0 /O1 /Gy /Gw "/I$sdk/include" "/Fe$fifoTest" "/Fo$root/local/test_fifo_recycle.obj" "$sdk/libs/video/tests/test_fifo_recycle.c" /link /OPT:REF
    if ($LASTEXITCODE -ne 0) { throw 'FIFO recycling test compilation failed.' }
    & $fifoTest
    if ($LASTEXITCODE -ne 0) { throw 'FIFO recycling consumer acknowledgment test failed.' }
    $fpTest = Join-Path $root 'local/test_fp_decompiler.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$fpTest" "/Fo$root/local/" "$sdk/libs/video/tests/test_fp_decompiler.c" "$sdk/libs/video/rsx_fp_decompiler.c"
    if ($LASTEXITCODE -ne 0) { throw 'Fragment decompiler test compilation failed.' }
    & $fpTest
    if ($LASTEXITCODE -ne 0) { throw 'Fragment decompiler regression failed.' }
    $predicateTest = Join-Path $root 'local/test_fp_predication.exe'
    & $clang /nologo /EHsc /W0 /O1 "/I$sdk/include" "/Fe$predicateTest" "/Fo$root/local/" "$sdk/libs/video/tests/test_fp_predication.cpp" "$sdk/libs/video/rsx_fp_decompiler.c" /link d3d11.lib d3dcompiler.lib
    if ($LASTEXITCODE -ne 0) { throw 'Fragment predication test compilation failed.' }
    & $predicateTest
    if ($LASTEXITCODE -ne 0) { throw 'Fragment predication shader execution failed.' }
    $clearTest = Join-Path $root 'local/test_color_clear.exe'
    & $clang /nologo /W0 /O1 "/Fe$clearTest" "/Fo$root/local/test_color_clear.obj" "$sdk/libs/video/tests/test_color_clear.c" /link d3d12.lib dxgi.lib d3dcompiler.lib
    if ($LASTEXITCODE -ne 0) { throw 'Color clear test compilation failed.' }
    & $clearTest
    if ($LASTEXITCODE -ne 0) { throw 'D3D12 channel/scissor clear execution failed.' }
    $textureTest = Join-Path $root 'local/test_live_textures.exe'
    $textureLibrary = Join-Path $build $(if ($RuntimeOnly) { 'ps3recomp_runtime.lib' } else { 'ps3recomp_sdk/ps3recomp_runtime.lib' })
    & $clang /nologo /W0 /O1 /MD /Gy /Gw "/I$sdk/include" "/Fe$textureTest" "/Fo$root/local/test_live_textures.obj" "$sdk/libs/video/tests/test_live_textures.c" /link /OPT:REF $textureLibrary
    if ($LASTEXITCODE -ne 0) { throw 'Live texture test compilation failed.' }
    & $textureTest
    if ($LASTEXITCODE -ne 0) { throw 'Live texture cache and D3D12 upload test failed.' }
    $saveTest = Join-Path $root 'local/test_savedata_persistence.exe'
    & $clang /nologo /W0 /O1 /Gy /Gw "/I$sdk/include" "/Fe$saveTest" "/Fo$root/local/test_savedata_persistence.obj" "$sdk/libs/system/tests/test_savedata_persistence.c" /link /OPT:REF
    if ($LASTEXITCODE -ne 0) { throw 'Save persistence test compilation failed.' }
    & $saveTest
    if ($LASTEXITCODE -ne 0) { throw 'Save persistence and missing-load result test failed.' }
    $sailTest = Join-Path $root 'local/test_sail_lifecycle.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$sailTest" "/Fo$root/local/test_sail_lifecycle.obj" "$sdk/libs/codec/tests/test_sail_lifecycle.c"
    if ($LASTEXITCODE -ne 0) { throw 'SAIL lifecycle test compilation failed.' }
    & $sailTest
    if ($LASTEXITCODE -ne 0) { throw 'SAIL lifecycle ABI test failed.' }
    $dialogTest = Join-Path $root 'local/test_dialog_callbacks.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$dialogTest" "/Fo$root/local/test_dialog_callbacks.obj" "$sdk/libs/system/tests/test_dialog_callbacks.c"
    if ($LASTEXITCODE -ne 0) { throw 'Dialog callback test compilation failed.' }
    & $dialogTest
    if ($LASTEXITCODE -ne 0) { throw 'Dialog callback order test failed.' }
    $fileTest = Join-Path $root 'local/test_host_file64.exe'
    & $clang /nologo /W0 /O1 "/Fe$fileTest" "/Fo$root/local/test_host_file64.obj" "$sdk/runtime/platform/tests/test_host_file64.c"
    if ($LASTEXITCODE -ne 0) { throw 'Large-file test compilation failed.' }
    & $fileTest
    if ($LASTEXITCODE -ne 0) { throw 'Large-file stat and seek test failed.' }
    $memoryTest = Join-Path $root 'local/test_memory_container.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$memoryTest" "/Fo$root/local/test_memory_container.obj" "$sdk/runtime/syscalls/tests/test_memory_container.c"
    if ($LASTEXITCODE -ne 0) { throw 'Memory-container test compilation failed.' }
    & $memoryTest
    if ($LASTEXITCODE -ne 0) { throw 'Memory-container allocation test failed.' }
    $ovisTest = Join-Path $root 'local/test_ovis_elf.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$ovisTest" "/Fo$root/local/test_ovis_elf.obj" "$sdk/libs/misc/tests/test_ovis_elf.c"
    if ($LASTEXITCODE -ne 0) { throw 'Overlay ELF query test compilation failed.' }
    & $ovisTest
    if ($LASTEXITCODE -ne 0) { throw 'Overlay ELF query ABI test failed.' }
    $elfTest = Join-Path $root 'local/test_elf_overlay.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$elfTest" "/Fo$root/local/test_elf_overlay.obj" "$sdk/runtime/spu/tests/test_elf_overlay.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU ELF loader test compilation failed.' }
    & $elfTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU ELF overlay isolation test failed.' }
    $bindingTest = Join-Path $root 'local/test_overlay_binding.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$bindingTest" "/Fo$root/local/test_overlay_binding.obj" "$sdk/runtime/spu/tests/test_overlay_binding.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU overlay binding test compilation failed.' }
    & $bindingTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU overlay content binding test failed.' }
    $lookupTest = Join-Path $root 'local/test_code_lookup.exe'
    & $clang /nologo /W0 /O1 "/I$sdk/include" "/Fe$lookupTest" "/Fo$root/local/test_code_lookup.obj" "$sdk/runtime/spu/tests/test_code_lookup.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPU resident lookup test compilation failed.' }
    & $lookupTest
    if ($LASTEXITCODE -ne 0) { throw 'SPU resident lookup test failed.' }
    Write-Output 'SPU segment selection, SPURS queues, game-data callbacks, DMA watchdog and isolation, taskset dispatch, SAIL lifecycle, dialog callbacks, large-file, memory-container, overlay staging/binding, and SPU ELF loader tests passed.'
}

