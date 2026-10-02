# BUILD FZG1 v3.3.0 — A: 核心解包 / 拼裝 / config 比對 (Stage 1 Worker-1A)

日期：2026-10-01｜分支：dm3q-fzg1-v330-rebuild（未切分支）｜worktree 外產物：`/mnt/240G_SSD/wt-fzg1-kernel/`

> 與 1B 分工：本檔只記 1A（解包＋拼裝＋olddefconfig/modules_prepare＋diffconfig）；工具鏈/網路缺口見 `BUILD-FZG1-v3.3.0-B.md`。本次 Stage 1 新增的未提交檔為本檔 + B.md；`live-FZG1-config.gz` 早已存在未提交；核心源樹未進 repo。

## 1. 解包

- 來源 `/mnt/240G_SSD/s9180-fzg1/SM-S9180_HKTW_16_Opensource.zip`（638M）：`Kernel.tar.gz`＋`Platform.tar.gz`（保留不動）。
- 解包到 `/mnt/240G_SSD/wt-fzg1-kernel/src/`（3.3G）：`build_kernel_GKI.sh`＋`kernel_platform/`（`common/`＋`msm-kernel/`＋`build/`）＋`vendor/qcom/opensource/`（techpack 在 `src/vendor/`）。

## 2. prebuilts clang / LLVM_VERSION

- `kernel_platform/prebuilts` **不存在**，無自帶 clang（`external/` 僅 dtc 等，`gcc/` 空殼，`qcom/` 僅 `proprietary/prebuilt_HY11`）。
- `common/build.config.constants`（msm-kernel/common 一致）：`BRANCH=android13-5.15`，`CLANG_VERSION=r450784e`（即 live 的 `clang 14.0.7 Android 8508608`；`CLANG_PREBUILT_BIN=prebuilts/clang/.../clang-r450784e/bin` 懸空）。
- 實測用 NDK r29 clang **21.0.0**＋host `flex 2.6.4/bison 3.8.2/pahole 1.31/gcc 15.2.0`（flex/bison 本次補裝）。

## 3. 拼裝結論（dm3q user）

無 dm3q defconfig（MODEL/REGION 只命名 dtbo）。`build_kernel_GKI.sh`（`BUILD_TARGET=dm3q_chn_hkx`，`user`，`kalama`）→ `prepare_vendor.sh sec gki`。鏈：`build.config`→`build.config.msm.kalama.sec`→（`build.config.msm.kalama`＋`msm.common`＋`msm.gki`）＋`build.config.sec`。user 版＝`gki_defconfig`＋`kalama_GKI.config`＋頂層 `msm-kernel/lego.config`＋`kalama_sec_defconfig`（eng/userdebug 片段跳過；另有 GKI 側 `TRIM_NONLISTED_KMI=1`）。源樹 `5.15.178` vs live `5.15.189`（見 §6 決議）。

## 4. olddefconfig + modules_prepare（KDIR=msm-kernel，ARCH=arm64 LLVM=1）

- `olddefconfig` **成功**（7112→7677 行）。
- `modules_prepare` **失敗（預期）**：`TRIM_UNUSED_KSYMS` whitelist 指向三星內網絕對路徑 `…/qb5_8814/…/abi_symbollist.raw` 不存在（`autoksyms.h Error 1`）。
- 對照：whitelist 改指空 stub 後 `modules_prepare` **成功**（`out-stub/`，exit 0），證明 NDK r29 工具鏈可備 headers，blocker 僅 whitelist 路徑。

## 5. 差異表（`scripts/diffconfig` live→new，全文在 `/mnt/240G_SSD/wt-fzg1-kernel/logs/diffconfig.txt`，584 行：`-`94/`+`485/`->`5）

| 項 | 結論 |
|---|---|
| 工具鏈戳記 `->`5 | `CLANG/AS/LLD 140007→210000`、`CC_VERSION_TEXT 14.0.7→21.0.0`、`PAHOLE 123→131` 而已 |
| LTO/CFI/MODVERSIONS/WHITELIST（指定項） | **全一致**：`LTO_CLANG_FULL`、`CFI_CLANG+SHADOW`、`MODVERSIONS=y`、`MODULE_FORCE_LOAD=n`、`TRIM=y`＋whitelist 原文保留 |
| `-`94 | 三星閉源符號被丟（`SAMSUNG_*/DEFEX*/FIVE*/PROCA*/SDFAT*/STLOG*` 等；單樹 Kconfig 無此符號） |
| `+`485 | Kconfig 預設補 `n`（`ARCH_KALAMA/CROW/…`、`SENSORS_*/SND_SOC_*`、`SEC_FACTORY` 等） |

 產物：`/mnt/240G_SSD/wt-fzg1-kernel/{Kernel.tar.gz,src/,out/,out-stub/,logs/{live.config,diffconfig.txt,olddefconfig.log,modules_prepare.log,modules_prepare-stub.log}}`；詳版見該目錄 `SUMMARY-1A.md`。

## 6. Stage 1 review 決議（2026-10-01，兩份 review 後定案）

- **178 vs 189 策略**：先用本樹 + release 字串覆寫往前走，`audit --manual-relocation`（零缺失/零 CRC）作客觀 gate。若 audit 因 stable 漂移失敗，再取 exact 189 樹（`SM-S916B_16_Opensource`，驗 `5.15.189 + changelist 33413713`）。`-94` 三星閉源符號缺失是已知條件，編譯期若 Samsung patch 引用到缺失符號，以實際編譯錯誤為準處理。
- **stub 定性**：`out-stub/` 空 whitelist 僅為工具鏈對照，嚴禁流入正式 `.ko`。正式走 REBUILD §7 路線（空 whitelist + vmlinux 重建 symvers，manual 路線保持 `__versions` 空）。
- **r25c**：批准下載 `android-ndk-r25c-linux.zip`（~506MiB，`dl.google.com`，裝到 `~/Android/Sdk/ndk/25.2.9519653`），下載後驗 `clang --version` 含 `14.0.7` 才算數。Stage 2 用。
- **ksu_props**：整 repo 404，ksud exact-rev 阻塞；`.ko` 構建不依賴它，先行。ksud 方案排序：真品 mirror > 顯式降級 `699849f` + 重驗 > 無限期等，Stage 3 定案。
