"""Container entry point: host checks, source rebuild and SD-card ZIP."""
from pathlib import Path
import argparse, os, subprocess, tempfile, zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/output'))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='deadzone-build-') as scratch:
        scratch = Path(scratch)
        for name in ('deadzone_test', 'profiles_test'):
            executable = scratch/name
            subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            str(ROOT/'tests'/(name+'.cpp')), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
        subprocess.run(['python3', str(ROOT/'tests/gecko_address.py')], check=True)
        wps = output/'WWHDDeadzone.wps'
        subprocess.run(['python3', str(ROOT/'build.py'), '--sdk', os.environ['DEVKITPRO'],
                        '--build-dir', str(scratch/'ppc'), '--output', str(wps)], check=True)
        with zipfile.ZipFile(output/'WWHD-Deadzone-Tool.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.write(wps, 'wiiu/environments/aroma/plugins/WWHDDeadzone.wps')
        print('Built', output/'WWHD-Deadzone-Tool.zip')

if __name__ == '__main__':
    main()
