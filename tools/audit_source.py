"""Reject binary build outputs in the tracked source tree."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.o', '.obj', '.a', '.so', '.dll', '.exe', '.elf', '.map', '.bin', '.rpl', '.rpx', '.wps', '.wuhb', '.zip', '.7z', '.gz', '.tar'}

def main():
    paths = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z']).decode().split('\0')
    count = 0
    for name in filter(None, paths):
        path = ROOT/name
        if not path.is_file():
            raise ValueError('Missing tracked source file: '+name)
        if name.startswith(('relink/', 'build/', 'dist/')) or path.suffix.lower() in FORBIDDEN:
            raise ValueError('Generated/binary artifact tracked: '+name)
        data = path.read_bytes()
        if b'\0' in data:
            raise ValueError('Binary data tracked: '+name)
        data.decode('utf-8-sig')
        count += 1
    print('Audited', count, 'tracked text files; no generated binaries or archives.')

if __name__ == '__main__':
    main()
