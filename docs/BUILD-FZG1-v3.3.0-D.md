# BUILD FZG1 v3.3.0 — D: ksu_props 斷鏈解法 (Stage 2 Worker-2B)

日期：2026-10-01｜分支：dm3q-fzg1-v330-rebuild（未切分支）｜構建：未跑（1A 負責）
暫存：`/tmp/ksud-mirror-test/`、`/tmp/ksud-downgrade-test/`、`/tmp/resetprop-rs-bare/`、`/tmp/ksu_props-verify/`

> 分工：本檔只記 2B（ksu_props 真品/降級驗證）。核心構建見 A.md；工具鏈見 B.md。
> 保證：repo 內二進位/feed 未動；本任務僅新增本檔；不提交。

## 1. Pin（v3.3.0 上游原樣）

- `userspace/ksud/Cargo.toml:58`：
  `prop-rs-android = { git = "https://github.com/Kernel-SU/ksu_props", rev = "6f5723105d8d4cacad31d83d343defbf032c7b33" }`
- `Cargo.lock`：`prop-rs` + `prop-rs-android` 皆 `0.2.0`，
  `source = "git+https://github.com/Kernel-SU/ksu_props?rev=6f57231...#6f57231..."`。
- 現狀重驗：`git ls-remote https://github.com/Kernel-SU/ksu_props`（HEAD 與該 rev）皆
  `Repository not found`；`api.github.com/repos/Kernel-SU/ksu_props` 回 404。整 repo 消失，非單一 rev 缺失。

## 2. 真品 mirror：有（hash 完全一致）

- **結論：有真品。`https://github.com/5ec1cff/resetprop-rs` 含 exact rev。**
- 依據：該 repo 即 ksu_props 頁面所載上游（`Forked from 5ec1cff/resetprop-rs`），公開可取，
  `git ls-remote HEAD → ddb6ee7`。
- Hash 比對（Cargo.lock 所載為準）：
  - Cargo.lock pin：`6f5723105d8d4cacad31d83d343defbf032c7b33`
  - `git -C /tmp/resetprop-rs-bare rev-parse 6f57231` → **`6f5723105d8d4cacad31d83d343defbf032c7b33`，byte-identical，比對通過**
  - commit 主旨：`test: ensure options are before arguments (ksu require this)`，2026-06-29。
  - 同庫亦含 `699849f3e4db47b622bcb7fae8e76c728b1b65a6`（"bump to 0.2.0"），兩 rev 同源可 diff。
- 等價性：同 rev 下 `Cargo.toml` workspace（`prop-rs` / `prop-rs-android`，`version.workspace = true = 0.2.0`）
  與本地 `699849f` 殘留快照同結構，包名一致，可直接換 URL。
- crates.io：`prop-rs`、`prop-rs-android` 皆不存在
 （`GET /api/v1/crates/prop-rs-android → {"errors":[{"detail":"crate does not exist"}]}`，
  sparse index `pr/op/prop-rs` → `NoSuchKey`）；無等價 crate。pip 與 Rust 依賴無關，未用。
- GitHub fork/mirror：原 `Kernel-SU/ksu_props` 404 後其 fork 列表不可見，未另尋第三方拷貝；
  既有父庫即真品，無需第三方。

### 真品試編（/tmp 隔離，不動 repo）

- 方法：複製 `/tmp/opencode/ksu-v330-s1` → `/tmp/ksud-mirror-test`，
  僅改 `userspace/ksud/Cargo.toml:58` URL 為 `https://github.com/5ec1cff/resetprop-rs`，rev 不變。
- `cargo fetch` → exit 0（`Checking prop-rs-android v0.2.0 (...5ec1cff/resetprop-rs?rev=6f57231...#6f57231)`）。
- `cargo check`（host，需 NDK clang 在 PATH 以過 `build.rs` LKM bootstrap 組裝）→ exit 0。
- **`cargo check --target aarch64-linux-android`（`ANDROID_NDK_HOME=r29`）→ exit 0**
  （`Checking prop-rs-android ... Finished dev profile`）。ksud Android 代碼（含 `rebuild_all`）一次通過。

## 3. (b) 降級試編：本地 699849f 可取但編不過 Android 目標

- 方法：同上複製 → `/tmp/ksud-downgrade-test`，rev 覆寫為本地已有的
  `699849f3e4db47b622bcb7fae8e76c728b1b65a6`（URL 不變，命中 `~/.cargo/git` 現存 db/checkout，無需上游）。
- `cargo fetch` → exit 0（`Locking ... rev=699849f...`；屬離線可用殘留，非上游恢復）。
- `cargo check`（host）→ exit 0 ——**誤導性通過**：`resetprop.rs`/`magica.rs`/`init_event.rs` 的
  prop 調用全在 `#[cfg(target_os = "android")]` 下，host 不編該代碼（rustc 行無 `--extern prop_rs_android`）。
- **`cargo check --target aarch64-linux-android` → exit 101，失敗（符合預期）：**
  `error[E0599]: no method named rebuild_all found for struct ResetProp`（`userspace/ksud/src/resetprop.rs:205`）。
- 根因（`git diff 699849f..6f57231`，5 檔 +96/-22）：舊 `ResetProp::rebuild(&String)` 單參數、
  無 `rebuild_all`；新版 `rebuild` 內調雙 area + 新增 `rebuild_all(force)`、
  `sys_prop::rebuild(ctx, check, appcompat)` 三參數 + `list_contexts`。v3.3.0 ksud
 （`rp.rebuild(&ctx)` + `rp.rebuild_all(cli.force)`）依賴新 API，降級必斷。
- 故 (b) 不是「換 rev 即編過」：需另寫相容墊片（把 ksud 調用改回舊 API，行為降級：丟 `-c` 重建语义/appcompat 雙區），
  屬源碼改動 + 語義降級，僅作無 mirror 時的最後手段。

## 4. Stage 3 ksud 建議路線

1. **首選真品 mirror（本檔 §2）：Stage 3 採用 `https://github.com/5ec1cff/resetprop-rs` @ 同 rev
   `6f57231...` 做 ksud 構建。** 改動僅 `userspace/ksud/Cargo.toml:58` 的 git URL，rev/hash/Cargo.lock 語義不變，
   無降級、無墊片。建議附帶記錄換源理由（上游 fork 父庫、hash 已驗），並在 Stage 3 重跑
   `cargo fetch` + `cargo check --target aarch64-linux-android` + `cargo ndk -t arm64-v8a --platform 24 build --release`
  （`KSU_VERSION/VERSION_CODE=32601`）全鏈。
2. (b) 降級不推薦：已證 Android 目標編譯失敗，需改 ksud 源碼墊片且丟重建語義；僅當 mirror 亦消失時啟用，
   且必須標註「降級構建」並重驗 `resetprop -c` 行為。
3. 無限期等上游恢復不必要：真品已在手；`Kernel-SU/ksu_props` 若日後恢復，可無縫換回原 URL（同 hash 即同內容）。

## 5. 產物與保證

- 新增：本檔 `docs/BUILD-FZG1-v3.3.0-D.md`；repo 內其餘未動（`git status` 僅此一新檔）。
- 二進位/feed：未改、未提交。構建驗證全在 `/tmp`（`ksud-mirror-test`、`ksud-downgrade-test`、
  `resetprop-rs-bare`、`ksu_props-verify`），可刪。
