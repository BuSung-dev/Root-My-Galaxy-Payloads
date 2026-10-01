# BUILD FZG1 v3.3.0 — F: ksud v3.3.0 重建 (Stage 3 Worker-3B)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切分支）｜構建：`/tmp/ksud-3B-v330`（repo 外隔離）
重型產物：`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`（repo 外；不進 repo/不進 feed）
repo 新增：僅本檔 `docs/BUILD-FZG1-v3.3.0-F.md`；不提交；repo 內二進位與 feed 未動。

> 分工：本檔只記 3B（ksud userspace v3.3.0 重建）。`.ko` 見 C.md（FZG1 獨立）+ E.md（3A S918B 正式資產）；
> ksu_props 路線見 D.md；拼裝/config 見 A.md；工具鏈見 B.md；loader 決策見 `kernelsu/REBUILD-dm3q-v3.3.0.md §0`。
> 保證：Stage 4 硬體驗證前不發布；本任務不覆寫 `kernelsu/ksud-*` 任一現存二進位，不動 feed。

## 0. 結論先行

- **ksud 有產出**：`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`，
  **5101488 B**，SHA-256 `aab769430467e5a9f723c1bc8a3180e6fb0b0b82c52fba7b99887199407fc940`。
- **版本驗證**：`strings` 含 `3.3.0` + `32601` + `3.3.0 (uapi: 2)`；`git rev-list --count HEAD = 2601`
  → `30000+2601 = 32601`，`git describe --tags = v3.3.0`；與 `KSU_VERSION=32601` 一致。
  直接執行 `ksud -V` 不可在 x86_64 host 跑（無 qemu-aarch64；aarch64 靜態執行環境缺失），以 strings + git + build.rs 邏輯代驗（見 §4 限制）。
- **正式資產，非 smoke**：內嵌的是 **3A 新 S918B 資產**（E.md，
  `kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko`，364584 B，`7ee01a06…49ac7b`，vermagic
  `abS916BXXSAFZG1`，v3.3.0 + 主 patch + dm3q build-fix + RKP early-return）。
  任務允許的 smoke 路線（既有 dm2q v3.2.5 `.ko`）**未採用**——3A 在本任務構建前已產出（20:08），故按「優先等 3A」走正式資產。
- **限制（必讀）**：`--platform 24` 在 v3.3.0 下**編不過**（link 失敗，見 §3）；
  本次用 `--platform 26`（上游 `ksud.yml` 同值）。產物 `file` 為 `for Android 26`，
  與既有 v3.2.5 ksud（`for Android 24`）差一水位；`built by NDK r29` + `not stripped` 仍對齊。
  未做硬體驗證（Stage 4 才做）；未進 repo/未進 feed。

## 1. 上游 + mirror（任務 1）

- 基樹：`/tmp/ksud-3B-v330` 由 `ksu-S918B-v330`（3A 樹，上游 `v3.3.0` / `932014ab`，
  + 主 patch + dm3q build-fix + RKP early-return，userspace 與 `ksu-v3.3.0` 同文）`cp -a` 而來。
  選 3A 樹理由：ksud userspace 同文（`diff -r userspace` 無差），且 asset 路徑與 3A 同樹，省一次換樹。
  `git describe --tags = v3.3.0`，`rev-list --count HEAD = 2601`（→ 32601，見 §4）。
- `userspace/ksud/Cargo.toml:58` 僅換 URL，不換 rev：
  - 改前：`prop-rs-android = { git = "https://github.com/Kernel-SU/ksu_props", rev = "6f5723105d8d4cacad31d83d343defbf032c7b33" }`
  - 改後：`prop-rs-android = { git = "https://github.com/5ec1cff/resetprop-rs", rev = "6f5723105d8d4cacad31d83d343defbf032c7b33" }`
- tree hash 驗（前置已驗，本次重驗通過）：
  - `/tmp/resetprop-rs-bare rev-parse 6f57231` → `6f5723105d8d4cacad31d83d343defbf032c7b33`（byte-identical）
  - `rev-parse '6f57231^{tree}'` → `0920308f410333283ca39cc56946527ed989eed4`（前置 `0920308f` 前綴一致）
  - commit 主旨 `test: ensure options are before arguments (ksu require this)`。
- cargo fetch hash 驗（`ANDROID_NDK_HOME=r29`，`cargo fetch` exit 0）：
  - `Cargo.lock` 寫入 `source = "git+https://github.com/5ec1cff/resetprop-rs?rev=6f57231...#6f57231..."`，
    `prop-rs` + `prop-rs-android` 皆 `0.2.0`（URL 換源、rev/hash 語義不變）。
  - `~/.cargo/git/checkouts/resetprop-rs-4a3f0c1997ee3783/6f57231`：
    `rev-parse HEAD = 6f5723105d8d4cacad31d83d343defbf032c7b33`，
    `HEAD^{tree} = 0920308f410333283ca39cc56946527ed989eed4`，與上游 bare 一致。
- `cargo check --target aarch64-linux-android`（`ANDROID_NDK_HOME=r29`，NDK clang 在 PATH）→ **exit 0**
  （`Checking prop-rs-android v0.2.0 (...5ec1cff/resetprop-rs?rev=6f57231...)`；
  唯一 warning 為 `utils.rs:360 daemonize never used`，屬 Samsung staging-rename 後的死碼殘留，預期）。

## 2. 資產 stage + 構建（任務 2）

- 資產（正式，非 smoke）：
  `cp /mnt/240G_SSD/wt-fzg1-kernel/kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko`（3A 新件，364584 B，
  `7ee01a06…49ac7b`，`modinfo` vermagic `5.15.189-android13-8-33413713-abS916BXXSAFZG1`，
  `scmversion g932014ab5b2c-dirty`）
  → `/tmp/ksud-3B-v330/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko`
  （ASSET 命名 `userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko` ✓；`sha256sum` 同 `7ee01a06…`）。
- 工具鏈：`cargo-ndk 4.1.2` ✓，`cargo 1.98.1`，NDK **r29**（`29.0.14206865`）✓，
  `export KSU_VERSION=32601 VERSION_CODE=32601 VERSION_NAME=3.3.0`（ksud 實際版本由 git 來，
  見 §4；export 僅保 kernel 側語義一致，不影響 `build.rs` 輸出）。
- 構建命令：`cargo ndk -t arm64-v8a --platform 26 build --release` → **exit 0**
  （`Finished release profile [optimized]`，24.07s；同上唯一 dead_code warning）。
  平台選型見 §3（24 編不過，26 為上游正解）。
- 輸出：`/tmp/ksud-3B-v330/target/aarch64-linux-android/release/ksud`，
  已另存 repo 外 `*.new`（見 §4）。

## 3. 平台 24 → 26 變更記錄（如實；任務前提过期）

- 任務原定 `cargo ndk -t arm64-v8a --platform 24 build --release` 在 v3.3.0 下 **exit 101，link 失敗**：
  `ld.lld: error: undefined symbol: __system_property_read_callback`（`referenced by ksud::utils::getprop`）。
- 根因：新 `prop-rs-android @6f57231`（v0.2.0）改用 `__system_property_read_callback`；
  NDK r29 sysroot 中該符號僅存在於 **API 26+** `libc.so`（實測 `llvm-nm`：24/21 缺，26/28/29/30/33/34/35 有），
  24 下無此符號故必斷鏈。既有 v3.2.5 ksud 用 `__system_property_get`（`llvm-nm -D` 實測），故當年 24 可編。
- 上游正解：`KernelSU/.github/workflows/ksud.yml` 明確 `setup-rust-build.sh <target> 26`
 （API level 26），NDK r29。D.md 只跑到 `cargo check`（不 link）故未暴露此斷點。
- 處置：平台改為 **26**，與上游一致。`file` 的 `for Android 26` vs 既有 `for Android 24`
  屬 v3.3.0 上游語義變更的必然結果，非本任務引入的漂移；`NDK r29` + `not stripped` 仍對齊（見 §4）。

## 4. 驗證（任務 3）+ 產物

| 項 | 結果 |
|---|---|
| `ksud -V` 直接執行 | 未跑（host 無 `qemu-aarch64`，aarch64 PIE 不可執行；如實記錄，非通過亦非失敗）。代驗：`strings` 含 `3.3.0`、`32601`、`3.3.0 (uapi: 2)`（`FULL_VERSION = "{VERSION_NAME} (uapi: {UAPI})"`）；`build.rs:get_git_version` = `rev-list --count 2601 → 32601` + `describe v3.3.0`；與 `KSU_VERSION=32601` 一致 |
| `strings 32601` | 有（且舊 ksud 同位置為 `32525`，新舊對照正確） |
| `file` | 新：`ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /system/bin/linker64, for Android 26, built by NDK r29 (14206865), not stripped`；舊（`ksud-dm3q-S918BXXSAFZF5-kdp`）：同串但 `for Android 24`。`NDK r29` ✓、`not stripped` ✓、`pie/aarch64/linker64` ✓；僅 `Android 26 vs 24` 差一水位（§3 已解釋） |
| 內嵌 `.ko` | `rust-embed` + `compression`，二進位內為壓縮形態，`vermagic`/`5.15.189` 明文不可 grep（預期）；stage 時 `sha256sum` 已驗 `7ee01a06…`（§2） |
| 產物（repo 外） | `/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`，**5101488 B**，SHA-256 `aab769430467e5a9f723c1bc8a3180e6fb0b0b82c52fba7b99887199407fc940`（`cp` 自 `target/.../release/ksud`；`file` 同上） |

- 量級對照：舊 `ksud-dm3q-S918BXXSAFZF5-kdp` 4879560 B；新 5101488 B（+221928 B，
  來自新 `.ko` 364584 vs 356928 + v3.3.0 staging-rename/metamodule 代碼增量 + API 26 libc stub 差異；量級合理）。
- repo 保證：`git status` 僅 `?? docs/BUILD-FZG1-v3.3.0-E.md`（3A 檔，本任務未動）+ 本檔；
  `kernelsu/ksud-*`、`kernelsu/*.ko`、feed 皆未改、未提交。

## 5. 限制與後續（Stage 4 前）

1. **不可發布**：本 `.new` 產物未做硬體驗證（late-load + `su -c id → u:r:ksu:s0` + Manager `Working <LKM>`），
   且 FZG1 loader 決策（REBUILD §0：FZG1 複用 S918B loader）+ feed 晉級由協調者 + Stage 4 定案。
2. **`-V` 未直接執行**：缺 qemu；Stage 4 在真機/模擬器上跑 `ksud -V/-v` + `ksu_get_info`（32601）補驗。
3. **Manager 配對**：須配 Manager **v3.3.0 / versionCode 32601**；勿與 3.2.5（32525）混用。
4. **平台水位**：v3.3.0 ksud 最低為 Android 26（上游定值）；FZG1（Android 13/14）運行無礙，
   但 `file` 不再是 `Android 24`，審計時以本檔 §3 為準，勿判為構建錯誤。
5. 可刪暫存：`/tmp/ksud-3B-v330`（含 `target/`）、`/tmp/resetprop-rs-bare`（前置遺留）；
   `/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new` 為本次唯一交付件（repo 外）。

## 回傳摘要（給協調者）

- ksud：**有**，`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`（5101488 B，
  `aab76943…fc940`）；版本代驗通過（strings `3.3.0`/`32601`/`3.3.0 (uapi: 2)`；git `2601→32601`/`v3.3.0`）。
- 資產：**正式資產**（3A 新 S918B `.ko`，`7ee01a06…49ac7b`），**非 smoke**。
- 限制：平台 24→26（上游要求，`file` 為 Android 26）；`-V` 未直接執行（無 qemu）；
  未硬體驗證、未進 repo/feed、不可發布。
