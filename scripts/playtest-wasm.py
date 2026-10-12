"""Run the browser build and keep authoritative logs, errors and screenshots."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


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
        browser = await playwright.chromium.launch(executable_path=args.chromium, headless=True)
        page = await browser.new_page(viewport={'width':1360, 'height':1000})
        page.on('console', lambda message: record(message.text))
        page.on('pageerror', lambda error: (errors.append(str(error)), record(str(error))))
        page.on('crash', lambda: (errors.append('Chromium page crashed'), record('Chromium page crashed')))
        query = ('smoke=gl' if args.webgl else 'smoke=1' if args.smoke else '')
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
        while time.monotonic() - started < args.seconds:
            if any('smoke] PASS:' in line for line in logs) or errors:
                break
            if (await page.locator('#status').text_content()).startswith('Runtime failed:'):
                break
            await asyncio.sleep(1)
        status = await page.locator('#status').text_content()
        await page.screenshot(path=str(output / 'screen.png'))
        passed = (args.smoke or args.webgl) and any('smoke] PASS:' in line for line in logs) and not errors
        report = {'smoke':args.smoke, 'webgl':args.webgl, 'passed':bool(passed), 'status':status, 'errors':errors,
                  'elapsed_seconds':time.monotonic()-started, 'gameplay_validated':False}
        (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        await browser.close()
    print(f'Evidence: {output}', flush=True)
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-deps', type=Path)
    parser.add_argument('--chromium')
    parser.add_argument('--url', default='http://127.0.0.1:8085/')
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--auto-assets', action='store_true')
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--webgl', action='store_true')
    parser.add_argument('--seconds', type=int, default=90)
    args = parser.parse_args()
    if args.python_deps:
        sys.path.insert(0, str(args.python_deps))
    return asyncio.run(run(args))


if __name__ == '__main__':
    raise SystemExit(main())
