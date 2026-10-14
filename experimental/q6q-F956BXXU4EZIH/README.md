# Fold6 EZIH shell source

Native source for the PomPomd SM-F956B firmware binding
`F956BXXU4EZIH`. The accompanying binaries are checked offline and have not
completed a root attempt on this firmware.

This snapshot preserves the shell payload's source and firmware tables.
It runs as the Android shell user through ADB or Shizuku. The source rejects
unrecognized kernel releases; the installer also checks the exact device
model and firmware before staging. Other entries in the inherited table do
not imply that their artifacts were rebuilt or validated by this update.

Build with Android NDK 29.0.14206865:

```sh
make NDK_ROOT=/path/to/android-ndk
```

On Windows:

```powershell
./build.ps1 -NdkRoot <ndk-directory>
```

Outputs are `build/payload` and `build/pompomsu-helper`. The native profile
assertions are retained in `tests/native_profiles.c`. These commands do not
build the separate KernelSU runtime.

`profile.json` records the distributed artifact hashes and support status.
The [firmware record](../../docs/SM-F956B-F956BXXU4EZIH.md) describes the
remaining hardware checks and Root My Galaxy's different app payload ABI.

## Credits

The shell code is derived from GhostLock-Galaxy and the projects credited by
its original authors:

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
- [YuKongA/ghostlock-app](https://github.com/YuKongA/ghostlock-app)
- [BuSung-dev/Root-My-Galaxy](https://github.com/BuSung-dev/Root-My-Galaxy)

The native shell source is distributed under [Apache 2.0](LICENSE). KernelSU
has its own license and build provenance; it is not covered by this license.
