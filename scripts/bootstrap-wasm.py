"""Fetch pinned, open-source shader compiler dependencies into ignored local/deps."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = [
    ('glslang', 'KhronosGroup/glslang', 'e1b562a8bed273a02f30b59b66a5d499793cede5'),
    ('glslang/External/spirv-tools', 'KhronosGroup/SPIRV-Tools', 'ef96ed763b43b59b33b31b362f09a02b729fa1c9'),
    ('glslang/External/spirv-tools/external/spirv-headers', 'KhronosGroup/SPIRV-Headers', '496543121ce6419f23d6fa5d7194ba66c36212d2'),
    ('spirv-cross', 'KhronosGroup/SPIRV-Cross', 'aa217aeb6c9f0ace7a0ab233b28807edf45eb165'),
]


def main():
    for name, repository, commit in DEPENDENCIES:
        destination = ROOT / 'local/deps' / name
        if (destination / '.git').exists():
            current = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
            if current != commit:
                raise RuntimeError(f'{destination} has {current}; expected {commit}. Use a fresh dependency directory.')
            continue
        if destination.exists() and any(destination.iterdir()):
            raise RuntimeError(f'Refusing to replace nonempty {destination}')
        destination.mkdir(parents=True, exist_ok=True)
        print(f'Fetching {repository} at {commit}', flush=True)
        subprocess.run(['git', 'init', '-q', str(destination)], check=True)
        subprocess.run(['git', '-C', str(destination), 'remote', 'add', 'origin', f'https://github.com/{repository}.git'], check=True)
        subprocess.run(['git', '-C', str(destination), 'fetch', '-q', '--depth', '1', 'origin', commit], check=True)
        subprocess.run(['git', '-C', str(destination), 'checkout', '-q', '--detach', commit], check=True)


if __name__ == '__main__':
    main()
