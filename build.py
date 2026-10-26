"""Build with devkitPPC, wut, and the official WUPS source checkout."""
from pathlib import Path
import argparse, subprocess, os, hashlib, json

root = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True, help='devkitpro root containing wut, devkitPPC and tools')
p.add_argument('--wups', type=Path, default=root/'third-party/WiiUPluginSystem', help='WUPS source (defaults to the included pinned source)')
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--output', type=Path, default=root/'WWHDDeadzone.wps', help='Output WPS; report is written beside it')
a = p.parse_args()
sdk, wups, build = a.sdk.resolve(), a.wups.resolve(), a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, DEVKITPRO=sdk.as_posix(), DEVKITPPC=(sdk/'devkitPPC').as_posix())
suffix = '.exe' if os.name == 'nt' else ''
cc = sdk/f'devkitPPC/bin/powerpc-eabi-g++{suffix}'
flags = ['-O2','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-DESPRESSO','-mcpu=750','-meabi','-mhard-float','-D__WIIU__','-D__WUT__','-D__WUPS__','-std=c++20','-I'+str(sdk/'wut/include'),'-I'+str(wups/'include')]
def run(args): subprocess.run([str(x) for x in args], check=True, env=env)
objects = []
units = ['main.cpp','wups_reent.cpp','button_combo.cpp','config_api.cpp','storage.cpp','WUPSConfigItemStub.cpp','WUPSConfigItemBoolean.cpp','WUPSConfigItemIntegerRange.cpp']
for name in units:
    src = wups/'libraries/libwups'/name
    obj = build/('wups_'+src.stem+'.o')
    run([cc,*flags,'-Wno-unused-parameter','-c',src,'-o',obj]); objects.append(obj)
for source in sorted((root/'source').glob('*.cpp')):
    obj = build/('plugin_'+source.stem+'.o')
    run([cc,*flags,'-c',source,'-o',obj]); objects.append(obj)
elf, wps = build/'WWHDDeadzone.elf', a.output.resolve()
wps.parent.mkdir(parents=True, exist_ok=True)
run([cc,*flags,'-specs='+str(sdk/'wut/share/wut.specs'),'-specs='+str(wups/'share/wups.specs'),'-T'+str(wups/'share/wups.ld'),'-Wl,-Map,'+str(build/'WWHDDeadzone.map'),'-o',elf,*objects,'-L'+str(sdk/'wut/lib'),'-L'+str(sdk/'wut/usr/lib'),'-lwut'])
run([sdk/f'tools/bin/elf2rpl{suffix}',elf,wps])
data = bytearray(wps.read_bytes())
assert data[:4] == b'\x7fELF'
data[9:11] = b'PL' # WUPS plugin marker; official wups_rules does the same.
wps.write_bytes(data)
run([sdk/f'devkitPPC/bin/powerpc-eabi-readelf{suffix}','-h',wps])
provenance = wups/'UPSTREAM.json'
commit = json.loads(provenance.read_text())['commit'] if provenance.exists() else subprocess.check_output(['git','-C',str(wups),'rev-parse','HEAD'], text=True).strip()
report = dict(compiled=True, hardware_tested=False, native_pro_capture='GamePad and Pro core restore verified after restart on GZ; see validation.json',
              wps_bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
              wups_commit=commit)
(wps.parent/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
