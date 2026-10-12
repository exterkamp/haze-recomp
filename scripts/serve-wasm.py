"""Serve the browser build locally with the headers required by pthreads."""
import argparse
import io
import json
import re
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


class Handler(SimpleHTTPRequestHandler):
    asset_roots = {}
    manifest = b''

    def translate_path(self, path):
        clean = unquote(urlsplit(path).path)
        parts = clean.strip('/').split('/')
        if len(parts) >= 2 and parts[0] == 'assets' and parts[1] in self.asset_roots:
            base = self.asset_roots[parts[1]]
            relative = '/'.join(parts[2:])
        else:
            base = ROOT / ('build/wasm' if clean.startswith('/build/') else 'web')
            relative = clean.removeprefix('/build/') if clean.startswith('/build/') else clean.lstrip('/')
        target = (base / relative).resolve()
        if not target.is_relative_to(base.resolve()):
            return str(base / '__invalid__')
        return str(target)

    def send_head(self):
        self.byte_range = None
        if urlsplit(self.path).path == '/assets/manifest.json':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(self.manifest)))
            self.end_headers()
            return io.BytesIO(self.manifest)
        target = Path(self.translate_path(self.path))
        if not target.is_file():
            return super().send_head()
        size = target.stat().st_size
        requested = self.headers.get('Range')
        start, end = 0, size - 1
        if requested:
            match = re.fullmatch(r'bytes=(\d*)-(\d*)', requested)
            if match and (match[1] or match[2]):
                if match[1]:
                    start = int(match[1])
                    end = min(int(match[2]), size - 1) if match[2] else size - 1
                else:
                    start = max(0, size - int(match[2]))
                if start > end or start >= size:
                    self.send_response(416)
                    self.send_header('Content-Range', f'bytes */{size}')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return None
                self.byte_range = (start, end)
        stream = target.open('rb')
        self.send_response(206 if self.byte_range else 200)
        self.send_header('Content-Type', self.guess_type(str(target)))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(end - start + 1))
        if self.byte_range:
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.end_headers()
        stream.seek(start)
        return stream

    def copyfile(self, source, outputfile):
        if self.byte_range is None:
            return super().copyfile(source, outputfile)
        remaining = self.byte_range[1] - self.byte_range[0] + 1
        while remaining:
            chunk = source.read(min(remaining, 1024 * 1024))
            if not chunk:
                break
            outputfile.write(chunk)
            remaining -= len(chunk)

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8085)
    shared_root = ROOT.parents[1] if ROOT.parent.name == '.worktrees' else ROOT
    parser.add_argument('--game-dir', type=Path, default=shared_root / 'game/disc')
    parser.add_argument('--firmware-dir', type=Path, default=shared_root / 'local/tools/rpcs3/dev_flash')
    args = parser.parse_args()
    Handler.asset_roots = {'disc':args.game_dir.resolve(), 'firmware':args.firmware_dir.resolve()}
    assets = {}
    for name, base in Handler.asset_roots.items():
        assets[name] = sorted(path.relative_to(base).as_posix() for path in base.rglob('*')
                              if path.is_file() and path.resolve().is_relative_to(base)) if base.is_dir() else []
    Handler.manifest = json.dumps(assets).encode('utf-8')
    with ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=ROOT / 'web')) as server:
        print(f'Haze browser build: http://127.0.0.1:{args.port}', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
