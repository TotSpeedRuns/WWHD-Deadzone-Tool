# Building WWHD Deadzone Tool

Install Docker with Linux-container support. No host devkitPPC, WUT or Python
installation is needed for the Docker route. The first build downloads the
toolchain image and packages; later builds reuse Docker's image cache.

Windows PowerShell, from the cloned repository:

```powershell
./build-docker.ps1
```

Linux/macOS with Bash:

```sh
bash build-docker.sh
```

Both scripts build a Linux amd64 image, run the host regression tests, and compile
the plugin. The source checkout is mounted read-only; compiler scratch files stay
inside the temporary container. Results go to `dist/`:

- `WWHDDeadzone.wps`
- `WWHD-Deadzone-Tool.zip`, containing only `wiiu/environments/aroma/plugins/WWHDDeadzone.wps`
- `build-report.json`, with compiler output hash and WUPS source revision

Pass an output directory as the script's first argument, or use PowerShell's
`-OutputDirectory`, to choose another location. Docker Desktop must be running;
on Windows it must use Linux containers. ARM hosts need amd64 emulation.

The base image is pinned by tag and digest in `Dockerfile`. WUPS is compiled from
the readable pinned source in `third-party/WiiUPluginSystem/`, not a precompiled
library ZIP. This fixes the toolchain image and dependency source used by the
build; it is not a claim of byte-for-byte identical binaries across build times
(the upstream plugin metadata includes a build timestamp).

## Rebuilding with modified WUPS

The root plugin source is MIT licensed. WUPS retains its own LGPL-3.0 license;
see `THIRD_PARTY_NOTICES.txt` and the accompanying LGPL/GPL texts. Edit the WUPS
source under `third-party/WiiUPluginSystem/`, then rerun the Docker build to
compile and link those changes into a new WPS. No committed `.o` or `.elf`
files are required. Install that WPS by copying it into the Aroma plugins folder
and fully restarting the console. Use Aroma Beta 26 or newer.

## Existing local SDK route

```sh
python build.py --sdk /path/to/devkitpro --build-dir build/local --output dist/WWHDDeadzone.wps
```

`--wups /path/to/WiiUPluginSystem` optionally selects another source checkout.
The default is the included source revision, recorded in `UPSTREAM.json`.
The SDK root must contain `devkitPPC/`, `wut/` and `tools/elf2rpl`.

The GitHub Docker workflow builds the WPS and runs regression tests. This checks
the build environment; it does not replace console capture/restart tests.
