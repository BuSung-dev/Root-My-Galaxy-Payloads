# BUILD FZG1 v3.3.0 — E: S918B 資產 v3.3.0 重建 + 審計 (Stage 3 Worker-3A)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切分支）｜worktree：`/mnt/240G_SSD/wt-fzg1-v330`
重型產物：`/mnt/240G_SSD/wt-fzg1-kernel/`（本次無新增二進位進 repo；`.ko` 留該目錄）
repo 新增：僅本檔 `docs/BUILD-FZG1-v3.3.0-E.md`；不提交。

> 背景：發布正線 = FZG1 複用 S918B loader（`kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp`，
> 內嵌單 KMI `android13-5.15_kernelsu.ko`，源自 dm2q 模組 vermagic
> `5.15.189-android13-8-33413713-abS916BXXSAFZG1`）。本任務重建此資產的 v3.3.0 版。
> 分工：FZG1 獨立 `.ko` 見 C.md；ksu_props/ksud 見 D.md + REBUILD §0–§5；拼裝/config 見 A.md。

## 1. 資產：有

- `/mnt/240G_SSD/wt-fzg1-kernel/kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko`
  **357K（364584 B）**，SHA-256
  `7ee01a065cd7f6743b2db2723620c8f6c4e61e0a050d98f541dc5cf79c49ac7b`
 （`llvm-strip -d` 後；`.symtab`/`.strtab` 保留，僅去 debug；量級與 dm2q 歷史 356928 B、FZG1 v3.3.0 358K 一致）。
- 未 strip 中間件：`ksu-S918B-v330/kernel/kernelsu.ko` 6.7M（同 FZG1 6.7M 量級）。

## 2. 源 + patch 序列（fresh clone，全乾淨無 fuzz）

- 上游：`git clone --branch v3.3.0 --depth 1 https://github.com/tiann/KernelSU.git`
  → `ksu-S918B-v330`，`932014ab`，`git describe --tags = v3.3.0` ✓（與 C.md FZG1 源同一 commit）。
- 依序打 patch（皆 exit 0、無 fuzz）：
  1. `kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`（主 patch）；
  2. `kernelsu/patches/KernelSU-v3.3.0-dm3q-5.15-build-fix.patch`
     （dm3q build-fix，`samsung_kdp.c:29-38` `>=5.16 rlimit_type / else ucount_type` 版本閘）；
  3. dm2q-fzg1 第二 hunk 語義（RKP early-return，6 行逐字同
     `KernelSU-v3.2.5-dm2q-fzg1.patch` 第二 hunk，插入於
     `kernel/hook/arm64/syscall_hook.c:218-226`，`if (!ksu_syscall_table) return;` 之後、
     `// Find one ni_syscall slot` 之前）：
     `#if defined(CONFIG_KSU_SAMSUNG_RKP)` / `pr_info("RKP build: syscall table patch is off\n");`
     / `ksu_dispatcher_nr = -1;` / `return;` / `#endif`。
     （REBUILD §6.4 記該 hunk 可 verbatim 攜帶；本次以 edit 落子，語義 byte-identical。
     與 FZG1 v3.3.0 版差異：FZG1 版**未**打此 hunk（C.md §3），故 FZG1 `.ko` 內無該字串，
     本 S918B `.ko` 內有——與 v3.2.5 dm2q 原件一致，見 §4。）
- Kconfig：`CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`
 （RKP hunk 與 `NO_PATCH_TEXT=y` 並存：前者關 syscall-table dispatcher，後者關 live text patch；
  即原 S918B「RKP syscall-table + live text patching disabled」路線的 v3.3.0 對應）。
- release（字面寫入）：`out/include/config/kernel.release`（無尾換行）與
  `out/include/generated/utsrelease.h` 皆覆寫為
  `5.15.189-android13-8-33413713-abS916BXXSAFZG1`；構建後已**恢復**
  `abS9180ZHS8FZG1`（FZG1 正式 headers 原狀；`/tmp/kernel.release.FZG1.bak`
  / `/tmp/utsrelease.h.FZG1.bak` 為一次性備份）。

## 3. tree / config 選型（如實）

- tree：**HKTW_16 S9180 樹**，`/mnt/240G_SSD/s9180-fzg1/SM-S9180_HKTW_16_Opensource.zip`
  → `wt-fzg1-kernel/src/`（`kernel_platform/msm-kernel`，Makefile `5.15.178`）。
- config：**FZG1 live config 衍生**，`live-FZG1-config.gz → out/.config`
  → r25c `olddefconfig`（7674 行，C.md §2 正式 out/，本次直接沿用，未重跑；
  `UNUSED_KSYMS_WHITELIST` 指空檔 `logs/abi_symbollist.empty`）。
- 工具鏈：模組側 NDK **r25c** clang 14.0.7（`r450784d1` wrapper，
  LLVM commit `4c603efb0...` 與 live `r450784e` 同源，C.md §1 已驗）。
- 構建命令（log `logs/kernelsu-ko-build-S918B-r25c.log`，**exit 0，0 error**；
  modpost undefined warnings 為 `KBUILD_MODPOST_WARN=1`＋空 symvers 下
  manual-relocation 預期形態；`BTF skipped (no vmlinux)` 預期）：
  `make -C out M=ksu-S918B-v330/kernel ARCH=arm64 LLVM=1 LLVM_IAS=1 CONFIG_KSU=m
  CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y CONFIG_KSU_SAMSUNG_DEFEX=y
  CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1
  KBUILD_MODPOST_WARN=1 modules`（PATH 首置 r25c llvm bin；`out/Module.symvers`
  保持不存在，使 `__versions` 為空）。
- 與原 S916B 樹差異（如實）：原 S918B 資產 `.ko`（dm2q `abS916BXXSAFZG1`）
  源自 **SM-S916B opensource 樹**（REBUILD §B Profile B：`SM-S916B_16_Opensource`
  + live FZG1 config + clang r450784e）；**S916B 樹不在本 host 上**
  （`/mnt/240G_SSD` 無 `*916B*`/`*918B*` 源包），**無法逐檔 diff**。
  已知：兩樹同 `33413713` build family、同 Kalama/GKI 基線；本樹 Makefile `5.15.178`
  vs 出貨 `5.15.189` 的 +11 stable drift 與 FZG1 情形相同（A.md §3/C.md §4），
  由 §4 audit 作客觀 gate。若 audit 因 drift 失敗才需取 exact-189 S916B 樹——本次通過，無需。

## 4. 審計（strip 前後一致；vs FZG1 vmlinux 作 proxy，先例如實）

| 項 | 結果 |
|---|---|
| `modinfo` vermagic | `5.15.189-android13-8-33413713-abS916BXXSAFZG1 SMP preempt mod_unload modversions aarch64` ✓（`scmversion g932014ab5b2c-dirty`，dirty 即三 patch，預期） |
| `readelf -SW \| grep __versions` | size `000000`（0）✓；`.symtab`/`.strtab` 保留 ✓ |
| 無 `stop_machine` 未定義引用 | ✓（no-patch-text 乾淨） |
| RKP 字串 | 本 `.ko` **含** `RKP build: syscall table patch is off`；FZG1 v3.3.0 `.ko` 無；v3.2.5 dm2q 原件有——路線保真 ✓ |
| `check_symbol <ko> /mnt/240G_SSD/s9180-fzg1/vmlinux.elf`（v3.3.0 tool） | **exit 0** ✓ |
| `audit_module_against_target.py --manual-relocation`（vs FZG1 `vmlinux.elf`＋`Module.symvers.FZG1` 8543 CRCs） | `undefined 200 / version entries 0 / missing 0 / kallsyms-resolved 64 / intentionally-without-CRC 200 / CRC mismatches 0`，**exit 0** ✓（數字與 FZG1 版 audit 完全一致；RKP hunk 不引入新 undefined symbol） |

## 5. 限制事項（未硬湊）

1. **S918B-exact vmlinux 缺**：host 僅有 S9180 FZG1 `vmlinux.elf`（51M）與
   FZF5 `vmlinux-fzf5.elf`；S918B（`abS918BXXSAFZF5`）exact ELF 不在 host 上。
   本次 audit 以 **FZG1 vmlinux 作 proxy**（先例如實；REBUILD §B 記 `.text`
   在 33413713 family 內相同，僅 `.data`/P0/slide 有差，故 proxy 有意義但非 exact）。
   S918B-exact audit 待該 ELF 到位後補跑。
2. **S916B 源樹差異未逐檔列出**：S916B opensource 樹不在 host 上（§3），
   只能記述選型與 family 層級等價 + audit gate，不能給 tree-diff 表。
3. **未做 ksud 重打包與硬體驗證**：本次僅產 `.ko`；`ksud-dm3q-S918BXXSAFZF5-kdp`
   的 v3.3.0（32601）重打包（REBUILD §4 流程：stage 新 `.ko` + r29 ksud build +
   late-load 硬體驗證）與 Manager v3.3.0 配對發布由後續 Stage 定案；
   本 `.ko` `KSU_VERSION 32601`，須與 32601 ksud 配對，不可與現存 v3.2.5 混用。
4. FZG1 複用關係不變：FZG1 正線仍複用此 S918B 資產內容（REBUILD §0）；
   本 `.ko` vermagic 後綴為 `abS916BXXSAFZG1`（與 dm2q 原件同），載入 FZG1 靠
   零 `__versions` kallsyms-aware manual loader（非 plain insmod）——與既有機制一致。

## 產物清單（`/mnt/240G_SSD/wt-fzg1-kernel/`，repo 外）

- `kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko`（357K 最終，SHA-256 `7ee01a06…9ac7b`）
- `ksu-S918B-v330/`（fresh v3.3.0＋三 patch 樹；未 strip `kernel/kernelsu.ko` 6.7M 在內）
- `logs/kernelsu-ko-build-S918B-r25c.log`（exit 0）
- 沿用：`out/`（已恢復 FZG1 release）、`Module.symvers.FZG1`、`check_symbol`
