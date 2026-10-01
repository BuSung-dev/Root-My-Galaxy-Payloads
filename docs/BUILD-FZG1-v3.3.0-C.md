# BUILD FZG1 v3.3.0 — C: r25c 工具鏈 + 正式 out/ + FZG1 獨立 .ko 構建與審計 (Stage 2 Worker-2A)

日期：2026-10-01｜分支：dm3q-fzg1-v330-rebuild（未切分支）
重型產物：`/mnt/240G_SSD/wt-fzg1-kernel/`（repo 內無二進位；`.ko` 留該目錄）
repo 新增：僅本檔 `docs/BUILD-FZG1-v3.3.0-C.md`；不提交。

> 分工：本檔只記 2A（r25c 下載安裝＋正式 out/ modules_prepare＋KernelSU v3.3.0 獨立 FZG1 `.ko`
> 構建＋四項審計）。拼裝/config 見 A.md；工具鏈缺口見 B.md；ksu_props 見 D.md；
> ksud/loader 側見 `kernelsu/REBUILD-dm3q-v3.3.0.md`（2B §0–§5＋2A 附錄 §6–§9）。
> 注意：REBUILD §Profile C 原定「FZG1 無獨立 KO、複用 S918B」；本次任務明確要求
> 按 S918B exact-source manual-relocation 路線為 FZG1 另編獨立 `.ko`
>（release 覆寫為 `abS9180ZHS8FZG1`），故產出獨立 FZG1 模組。是否取代複用路線由協調者定案。

## 1. r25c 下載＋安裝（任務 1）— 通過

- URL：`https://dl.google.com/android/repository/android-ndk-r25c-linux.zip`
- 下載到 `/tmp/opencode/android-ndk-r25c-linux.zip`：**531,118,193 bytes**，
  與 B.md §4 HEAD 預告一致（`~506MiB`）；SHA-1 `53af80a1cce9144025b81c78c8cd556bff42bd0e`（存檔備查）。
- 解包 `source.properties`：`Pkg.Revision = 25.2.9519653`，與安裝路徑修訂號一致。
- `clang --version`（安裝後路徑）：
  `Android (9352603, based on r450784d1) clang version 14.0.7
  (https://android.googlesource.com/toolchain/llvm-project 4c603efb0cca074e9238af8b4106c30add4418f6)`
  —— 含 `14.0.7` ✓。
- r450784e 同源驗證：live `CC_VERSION_TEXT` 為
  `Android (8508608, based on r450784e) clang 14.0.7 (.../llvm-project 4c603efb0cca074e9238af8b4106c30add4418f6)`；
  r25c clang LLVM commit `4c603efb0cca074e9238af8b4106c30add4418f6` **與 live 完全一致**，
  僅 Android build 號（`9352603` vs `8508608`）與 wrapper 修訂（`r450784d1` vs `r450784e`）差一水位——
  即 REBUILD 所述「同 LLVM commit」關係，判為同源通過。
- 安裝到 `~/Android/Sdk/ndk/25.2.9519653`（與 r27/r28/r29 並存）；zip 留 `/tmp/opencode`，解包中間目錄已移走。

## 2. 正式 out/ modules_prepare（任務 2，r450784e 路線）— 成功

- KDIR=`src/kernel_platform/msm-kernel`（`build.config: KERNEL_DIR=./msm-kernel`），
  `ARCH=arm64 LLVM=1 LLVM_IAS=1`，PATH 首置 r25c llvm bin（無 `CROSS_COMPILE`，沿用 1A 実測方式）。
- 依 REBUILD §7 路線：whitelist 指到**新建空檔**
  `logs/abi_symbollist.empty`（0 bytes，與 `out-stub` 所用 `logs/abi_symbollist.stub` 同為空內容、
  但檔名獨立；**正式 `out/` 全量重跑，不複用 `out-stub/` 任何產物**）。
  順序：`live-FZG1-config.gz → out/.config` → `scripts/config --set-str UNUSED_KSYMS_WHITELIST …empty`
  → `olddefconfig`（`No change to .config`，7674 行）→ `modules_prepare`。
- `modules_prepare` **exit 0**（log `logs/modules_prepare-r25c.log`，0 ERROR；
  另存 `logs/config.out-r29.bak`、`logs/kernel.release.orig`、`logs/utsrelease.h.orig` 備查）。
- `out/include/generated/autoksyms.h` 僅 `#define __KSYM_module_layout 1`（空 whitelist 預期形態）。
- 工具鏈戳記：`CONFIG_CLANG_VERSION 140007`，
  `CONFIG_CC_VERSION_TEXT "Android (9352603, based on r450784d1) clang version 14.0.7 …"`；
  較 1A 的 r29 戳記（210000/21.0.0）回到與 live 同代的 14.0.7。
  `PAHOLE 131` 維持 host 版（與 live 123 差異僅 BTF 工具，不影響 vermagic；記錄備查）。
- vmlinux 重建 symvers（供審計，不進 `out/`）：
  `extract_target_symvers.py /mnt/240G_SSD/s9180-fzg1/vmlinux.elf → Module.symvers.FZG1`，
  **8543 CRCs**（與 2A 附錄 FZF5 量測 8543 一致）。
  `out/Module.symvers` 保持**不存在**，使 modpost 產出空 `__versions`（manual-relocation 要求）。

## 3. KernelSU v3.3.0 獨立 FZG1 .ko（任務 3）— 產出

- 上游：`/mnt/240G_SSD/wt-fzg1-kernel/ksu-v3.3.0`，`git clone --branch v3.3.0 --depth 1`，
  `932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e`，`git describe --tags = v3.3.0` ✓。
- 打 patch（依序，皆乾淨、無 fuzz）：
  1. `kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`（主 patch）→ `MAIN-APPLY-OK`；
  2. `kernelsu/patches/KernelSU-v3.3.0-dm3q-5.15-build-fix.patch`（dm3q build-fix）→ `FIX-APPLY-OK`，
     `samsung_kdp.c:29-38` 為 `>=5.16 rlimit_type / else ucount_type` 版本閘（已重讀驗證）。
  未打 §6.4 RKP early-return hunk（任務指定僅主 patch＋dm3q build-fix；該 hunk 屬可選高保真攜帶）。
- release 覆寫（字面寫入）：`out/include/config/kernel.release` 與
  `out/include/generated/utsrelease.h` 皆為 `5.15.189-android13-8-33413713-abS9180ZHS8FZG1`。
- SELinux genheaders：`out/scripts/selinux/genheaders/genheaders` 產 `out/security/selinux/flask.h + av_permissions.h`。
- 編譯（log `logs/kernelsu-ko-build-r25c.log`，**exit 0，0 error**；modpost undefined warnings 為
  `KBUILD_MODPOST_WARN=1`＋空 symvers 下 manual-relocation 預期形態）：
  `make -C out M=ksu-v3.3.0/kernel ARCH=arm64 LLVM=1 LLVM_IAS=1 CONFIG_KSU=m
  CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y CONFIG_KSU_SAMSUNG_DEFEX=y
  CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1
  KBUILD_MODPOST_WARN=1 modules`
  （S918B no-patch-text 路線；`BTF skipped (no vmlinux)` 為預期——未做全樹 vmlinux）。
- 產物（repo 外）：未 strip 6.7M 於 `ksu-v3.3.0/kernel/kernelsu.ko`；
  `llvm-strip -d`（r25c）後最終 `kernelsu-dm3q-S9180ZHS8FZG1-v3.3.0.ko` **358K**
  （`9b4ace4d…a3abac`，`.symtab`/`.strtab` 保留，僅去 debug；量級與 dm2q 歷史 356928 B 一致）。

## 4. 審計（任務 4）— 四項全過（strip 前後一致）

| 項 | 結果 |
|---|---|
| `modinfo` vermagic | `5.15.189-android13-8-33413713-abS9180ZHS8FZG1 SMP preempt mod_unload modversions aarch64` ✓（`scmversion g932014ab5b2c-dirty`，dirty 即兩 patch，預期） |
| `readelf -SW \| grep __versions` | size `000000`（0）✓；`.symtab`/`.strtab` 保留 ✓ |
| `check_symbol <ko> /mnt/240G_SSD/s9180-fzg1/vmlinux.elf`（v3.3.0 tool，host gcc 建） | **exit 0** ✓ |
| `audit_module_against_target.py --manual-relocation`（vs FZG1 vmlinux.elf＋Module.symvers.FZG1） | `undefined 200 / version entries 0 / missing 0 / kallsyms-resolved 64 / intentionally-without-CRC 200 / CRC mismatches 0`，**exit 0** ✓ |

- 178 vs 189 漂移裁決：**audit 客觀通過，無需 exact-189 樹**。
  200 undef／64 kallsyms-resolved 與 2A 附錄「FZG1 複用 dm2q.ko 量測（200/0/64）」**數字完全一致**，
  證明源樹 5.15.178＋release 覆寫往前走的策略在本 target 上成立；`-94` 三星閉源符號缺失未轉為編譯錯誤。
- 附加：無 `stop_machine` 未定義引用（no-patch-text 乾淨）。

## 5. 阻塞項與後續

- 本任務鏈無阻塞：r25c ✓、modules_prepare ✓、`.ko`＋審計 ✓。
- 已知非阻塞：ksud userspace 仍走 D.md 真品-mirror 路線（Stage 3）；本 `.ko` 為 `KSU_VERSION 32601`
 （v3.3.0），須與 v3.3.0（32601）ksud 配對發布，不可與 repo 內現存 v3.2.5（32525）ko/ksud 混用。
- 決策留白：本獨立 FZG1 `.ko`（`abS9180ZHS8FZG1` vermagic）與 REBUILD §0「複用 S918B ksud、無獨立 KO」
  並存；晉級發布（取代／並列／僅備審）由協調者定案。硬體驗證（late-load＋`u:r:ksu:s0`＋Manager `<LKM>`）未在本次跑。
- 保證：repo 內僅新增本檔；`git status` 另見 D.md（平行 worker 產物，本任務未動）；
  二進位僅在 `/mnt/240G_SSD/wt-fzg1-kernel/`（`ksu-v3.3.0/` 樹、`kernelsu-dm3q-S9180ZHS8FZG1-v3.3.0.ko`、
  `Module.symvers.FZG1`、`check_symbol`、`logs/*-r25c*`）。

## 發布裁決（Stage 2 review，2026-10-01）

獨立 FZG1 `.ko`（vermagic `abS9180ZHS8FZG1` 精確）技術乾淨，但**不進 repo、不進 feed**：發布正線維持 REBUILD §0 複用路線（FZG1 複用 `ksud-dm3q-S918BXXSAFZF5-kdp`，已有硬體驗證真值；獨立 `.ko` 未做 late-load 硬體驗證，且 mint 新 ksud 會分裂 feed）。本 `.ko` 定位為構建可行性證明 / 備審件，留 `/mnt/240G_SSD/wt-fzg1-kernel/`。

## 產物清單（/mnt/240G_SSD/wt-fzg1-kernel/，repo 外）

- `ksu-v3.3.0/`（上游 v3.3.0＋雙 patch 樹；未 strip `kernel/kernelsu.ko` 6.7M 在內）
- `kernelsu-dm3q-S9180ZHS8FZG1-v3.3.0.ko`（358K 最終，SHA-256 `9b4ace4d…a3abac`）
- `Module.symvers.FZG1`（8543 CRCs，審計用）
- `check_symbol`（v3.3.0 tool 二進位）
- `out/`（r25c 重跑後的正式 headers＋覆寫 release）、`out-stub/`（未動）
- `logs/{abi_symbollist.empty,modules_prepare-r25c.log,kernelsu-ko-build-r25c.log,config.out-r29.bak,kernel.release.orig,utsrelease.h.orig}`
