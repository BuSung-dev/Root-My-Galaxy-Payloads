# BUILD FZG1 v3.3.0 — B: 工具鏈與網路缺口 (Stage 1 Worker-1B)

日期：2026-10-01 ｜分支：dm3q-fzg1-v330-rebuild ｜構建：未跑（1A 負責）

## 1. 網路結論

- `github.com`：**通**（`git ls-remote tiann/KernelSU HEAD` → `08a3b087`，~3.8s）。
- `crates.io`：**半通** — cargo sparse index（`index.crates.io`）正常，
  cargo 建置不受影響；HTML 前端對無 UA 的 curl 回 403（bot 過濾），非斷網。
- `dl.google.com`：**通**（r25c zip HEAD 回 200）。

## 2. cargo-ndk 狀態

- **已安裝 `cargo-ndk v4.1.2`**（`cargo install cargo-ndk` 成功，~24s），
  bins：`cargo-ndk / cargo-ndk-env / cargo-ndk-runner / cargo-ndk-test`。
- rustc/cargo 1.98.1。

## 3. ksu_props 狀態 — BLOCKER（比預期嚴重）

- v3.3.0 `userspace/ksud/Cargo.toml:58` pin：
  `prop-rs-android = { git = "https://github.com/Kernel-SU/ksu_props", rev = "6f57231..." }`
- **非單一 rev 404：整個 repo 消失** — `git ls-remote`（HEAD 與該 rev）
  皆 `Repository not found`；GitHub API 同報 404。
- `cargo fetch`（online）失敗：`revision 6f57231 not found` + http 404。
- 本地 cargo git cache 有殘留（`699849f` "bump to 0.2.0"），但缺所需 rev；
  `CARGO_NET_OFFLINE=true` 同樣失敗。
- 候選解法（留給 1A/協調者決策，本 worker 未動）：
  (a) 等上游恢復；(b) rev 覆寫為本地已有的 `699849f`（同 0.2.0 版線，或相容，需 diff）；
  (c) 找第三方 mirror/vendored 拷貝。搜尋索引仍有該 repo 2026-06 commit，屬近期消失。

## 4. NDK r25c 取得方案（待確認，未自動下載）

- 本地 `~/Android/Sdk/ndk/` 僅 r27（clang 18）/ r28c（clang 19）/ r29（clang 21），**無 r25x**。
- r25c 內含 clang 14.0.7（r450784e），與手機編譯器（Android 8508608）同源。
- URL：`https://dl.google.com/android/repository/android-ndk-r25c-linux.zip`
- 大小：**531,118,193 bytes（~506 MiB）**，HEAD 已驗 200。
- 安裝步驟（確認後執行）：
  1. `cd /tmp/opencode && curl -O <URL> && unzip -q android-ndk-r25c-linux.zip`
  2. `mv android-ndk-r25c ~/Android/Sdk/ndk/25.2.9519653`（與 `source.properties` 修訂號一致）
  3. 驗證 `.../toolchains/llvm/prebuilt/linux-x86_64/bin/clang --version` 含 14.0.7。
- 與現有 r27/28/29 並存無衝突。

## 5. 保證

- Repo 內二進位/feed：**未動**；本任務僅讀取 + 此 docs 記錄 + `/tmp` 暫存。
  （參考 clone：`/tmp/opencode/ksu-v330-s1 @ v3.3.0`；詳細 log：`/tmp/opencode/s1-worker1b-toolchain-report.md`）
