"""Run the browser build and keep authoritative logs, errors and screenshots."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


async def evaluate_gpu_workers(workers, expression):
    async def evaluate(owner):
        try:
            return await asyncio.wait_for(owner.evaluate(
                "(() => { const GLctx=globalThis.hazeGetGL?.(); return GLctx ? (" + expression + ")() : null; })()"), 1)
        except asyncio.TimeoutError:
            # Guest threads sleeping in Atomics.wait cannot service inspector
            # evaluations until they wake; never stall the input driver on them.
            return None
    return await asyncio.gather(*(evaluate(owner) for owner in workers))


async def profile_workers(browser, output, seconds):
    """Sample the actual guest pthreads, retaining Chrome's raw CPU profiles."""
    connection = await browser.new_browser_cdp_session()
    pending = {}
    sequence = 0
    def received(event):
        message = json.loads(event['message'])
        future = pending.pop((event['sessionId'], message.get('id')), None)
        if future:
            if 'error' in message:
                future.set_exception(RuntimeError(str(message['error'])))
            else:
                future.set_result(message.get('result', {}))
    connection.on('Target.receivedMessageFromTarget', received)
    async def command(session, method):
        nonlocal sequence
        sequence += 1
        request = sequence
        future = asyncio.get_running_loop().create_future()
        pending[(session, request)] = future
        await connection.send('Target.sendMessageToTarget', {
            'sessionId':session, 'message':json.dumps({'id':request, 'method':method})})
        return await asyncio.wait_for(future, 15)
    targets = (await connection.send('Target.getTargets'))['targetInfos']
    sessions = []
    for target in targets:
        if target['type'] != 'worker':
            continue
        session = (await connection.send('Target.attachToTarget', {'targetId':target['targetId'], 'flatten':False}))['sessionId']
        await command(session, 'Profiler.enable')
        await command(session, 'Profiler.start')
        sessions.append((session, target))
    await asyncio.sleep(seconds)
    for index, (session, target) in enumerate(sessions):
        result = await command(session, 'Profiler.stop')
        result['target'] = target
        (output / f'cpu-{index:02}.json').write_text(json.dumps(result), encoding='utf-8')
    await connection.detach()


async def run(args):
    from playwright.async_api import async_playwright
    output = ROOT / 'local/wasm-tests' / time.strftime('%Y%m%d-%H%M%S')
    output.mkdir(parents=True)
    logs, errors = [], []
    def record(text):
        logs.append(text)
        with (output / 'browser.log').open('a', encoding='utf-8') as stream:
            stream.write(text + '\n')
        print(text, flush=True)
    async with async_playwright() as playwright:
        # A regular profile matches the user's browser storage quota. Incognito
        # contexts cap it much lower and can reject Haze's game-data install.
        context = await playwright.chromium.launch_persistent_context(
            str((args.profile_dir or output / 'chromium-profile').resolve()),
            executable_path=args.chromium, headless=True,
            viewport={'width':1360, 'height':1000})
        browser = context.browser
        page = context.pages[0] if context.pages else await context.new_page()
        page.on('console', lambda message: record(message.text))
        page.on('pageerror', lambda error: (errors.append(str(error)), record(error.stack or str(error))))
        page.on('crash', lambda: (errors.append('Chromium page crashed'), record('Chromium page crashed')))
        query = ('smoke=audio' if args.audio else 'smoke=gl' if args.webgl else 'smoke=1' if args.smoke else '')
        if args.trace:
            query += '&trace=1'
        if not args.auto_assets:
            query += '&manual=1'
        await page.goto(args.url + '?' + query, wait_until='domcontentloaded')
        if not args.auto_assets:
            await page.locator('#disc').set_input_files(str(args.disc.resolve()))
            if args.firmware:
                await page.locator('#firmware').set_input_files(str(args.firmware.resolve()))
            await page.locator('#start').click()
        started = time.monotonic()
        last_capture = 0
        sound_enabled = False
        profiling = None
        input_stamp = None
        pending_actions = []
        while time.monotonic() - started < args.seconds:
            if any('smoke] PASS:' in line for line in logs) or errors:
                break
            if (await page.locator('#status').text_content()).startswith('Runtime failed:'):
                break
            if any('There is no free space to save game data' in line for line in logs):
                break
            if (args.sound or args.audio) and not sound_enabled and await page.locator('#sound').is_enabled():
                await page.locator('#sound').click()
                sound_enabled = True
            elapsed = time.monotonic() - started
            if args.input_file and args.input_file.exists():
                stamp = args.input_file.stat().st_mtime_ns
                if stamp != input_stamp:
                    input_stamp = stamp
                    action = json.loads(args.input_file.read_text(encoding='utf-8-sig'))
                    pending_actions = [(elapsed + item.get('after', 0), item)
                                       for item in action.get('actions', [action])]
            if pending_actions and elapsed >= pending_actions[0][0]:
                _, action = pending_actions.pop(0)
                if action.get('stop'):
                    break
                if action.get('reload'):
                    await page.reload(wait_until='domcontentloaded')
                    sound_enabled = False
                if action.get('evaluate'):
                    record('[test evaluate] ' + json.dumps(await page.evaluate(action['evaluate'])))
                if action.get('runtime_evaluate'):
                    for owner in page.workers:
                        if owner.url.endswith('/runtime-worker.js'):
                            record('[test runtime evaluate] ' + json.dumps(await owner.evaluate(action['runtime_evaluate'])))
                if action.get('gpu_evaluate'):
                    record('[test workers] ' + str(len(page.workers)))
                    # The renderer belongs to the initial 32-worker pool. Later
                    # workers execute SPU jobs and do not own a graphics context.
                    selected = [page.workers[index] for index in action.get('gpu_workers', range(33))
                                if index < len(page.workers)]
                    for result in await evaluate_gpu_workers(selected, action['gpu_evaluate']):
                        if result is not None:
                            record('[test GPU evaluate] ' + json.dumps(result))
                keys = action.get('keys', [])
                for key in keys:
                    await page.keyboard.down(key)
                await asyncio.sleep(min(3, max(0.05, action.get('hold_ms', 150) / 1000)))
                for key in keys:
                    await page.keyboard.up(key)
                recorded = dict(action)
                if 'runtime_evaluate' in recorded:
                    recorded['runtime_evaluate'] = '<evaluated>'
                if 'gpu_evaluate' in recorded:
                    recorded['gpu_evaluate'] = '<evaluated>'
                record('[test input] ' + json.dumps(recorded))
            if args.profile_after and elapsed >= args.profile_after and profiling is None:
                profiling = asyncio.create_task(profile_workers(browser, output, 10))
            if args.capture_interval and elapsed - last_capture >= args.capture_interval:
                await page.locator('#screen').screenshot(path=str(output / f'canvas-{int(elapsed):04}.png'))
                stats = await page.evaluate('window.hazeAudioStats || null')
                if stats:
                    record('[browser audio] ' + json.dumps(stats))
                if args.trace:
                    for worker in page.workers:
                        if worker.url.endswith('/runtime-worker.js'):
                            captures = await worker.evaluate("""() => {
                                if (!runtime?.FS) return {};
                                return Object.fromEntries(runtime.FS.readdir('/').filter(name => name.startsWith('wasm-gpu-'))
                                    .map(name => [name, runtime.FS.readFile('/' + name, {encoding:'utf8'})]));
                            }""")
                            for name, source in captures.items():
                                (output / name).write_text(source, encoding='utf-8')
                last_capture = elapsed
            await asyncio.sleep(1)
        status = await page.locator('#status').text_content()
        if profiling:
            try:
                await profiling
            except Exception as error:
                record('CPU profiling failed: ' + str(error))
        await page.screenshot(path=str(output / 'screen.png'))
        audio_stats = await page.evaluate('window.hazeAudioStats || null')
        canvas_pixel = await page.evaluate("""() => {
            const screen = document.querySelector('#screen');
            const copy = new OffscreenCanvas(screen.width, screen.height);
            const ctx = copy.getContext('2d'); ctx.drawImage(screen, 0, 0);
            return [...ctx.getImageData(screen.width / 2, screen.height / 2, 1, 1).data];
        }""")
        passed = (args.smoke or args.webgl or args.audio) and any('smoke] PASS:' in line for line in logs) and not errors
        if args.webgl:
            passed = passed and canvas_pixel == [51, 102, 153, 255]
        if args.audio:
            passed = passed and audio_stats and 0.2 < audio_stats['peak'] <= 0.25
        report = {'smoke':args.smoke, 'webgl':args.webgl, 'audio_smoke':args.audio, 'passed':bool(passed), 'status':status, 'errors':errors,
                  'elapsed_seconds':time.monotonic()-started, 'gameplay_validated':False,
                  'audio':audio_stats, 'canvas_pixel':canvas_pixel}
        (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        await context.close()
    print(f'Evidence: {output}', flush=True)
    return 0 if passed else 1


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    sys.stderr.reconfigure(encoding='utf-8', errors='backslashreplace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-deps', type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--profile-dir', type=Path)
    parser.add_argument('--input-file', type=Path)
    parser.add_argument('--url', default='http://127.0.0.1:8085/')
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--auto-assets', action='store_true')
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--sound', action='store_true')
    parser.add_argument('--capture-interval', type=int, default=0)
    parser.add_argument('--profile-after', type=int, default=0)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--webgl', action='store_true')
    parser.add_argument('--audio', action='store_true')
    parser.add_argument('--seconds', type=int, default=90)
    args = parser.parse_args()
    if args.python_deps:
        sys.path.insert(0, str(args.python_deps))
    return asyncio.run(run(args))


if __name__ == '__main__':
    raise SystemExit(main())
