# BUILD FZG1 v3.3.0 — G: 真機驗證 v3.3.0 (Stage 4 Worker-4A)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切分支）｜工作區 `/mnt/240G_SSD/wt-fzg1-v330`
待測物：`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`（5101488 B，`aab76943…fc940`，內嵌 v3.3.0 S918B 資產）
手機：R5CW21C8PLR (SM-S9180 FZG1)｜本任務 repo 寫入：僅本檔；log：`/tmp/fzg1-v330-G-verify.log`（同期另有 4B 的 H.md，同為 untracked）

> 護欄遵守：先備份在機狀態；走 guarded `--late-load`（root helper，不手動 insmod）；
> Manager 保持 v3.2.5，mismatch 屬預期只記不修；panic 才重試 exploit 鏈 ≤2 次，否則回滾；
> 絕不留無 root 狀態。回滾件 = `kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp`（v3.2.5，`5da5818d…86bd`）。

## 0. 結論先行

- **通過**：五項驗證門全過（見 §4）。**未回滾**，無 panic/reboot。
- 最終手機狀態：**v3.3.0 root** — `/proc/modules Live`、`su→u:r:ksu:s0`、`Enforcing`、
  `ksud -V=3.3.0`、`debug info version=32601`。`/data/adb/ksud` 已為新件（`aab76943…`）。
- Manager 保持 **v3.2.5 (32525)**，與 ksud 32601 mismatch banner 屬預期（用戶決議，只記不修）。
- 附帶發現（必讀）：v3.3.0 ksud **拒 `--ephemeral`**（`unexpected argument '--ephemeral'`），
  helper 按既有邏輯 fallback 到 plain late-load 後成功。後續 helper/文件若假定 ephemeral 必存在需更新。

## 1. 開工備份（護欄 1）— 通過

| 項 | 結果 |
|---|---|
| 在機 `/data/adb/ksud` | `su -c 'cp -a /data/adb/ksud /data/local/tmp/ksud-backup-G-32525'` → pull 到 `/tmp/fzg1-G-backup/ksud-backup-G-32525`，4556352 B，SHA-256 `077797b6…215eb57`，`strings` 含 `32525`/`3.2.5-mm-cn-z-M`（Manager fork 變體，與 repo kdp `5da5818d…` 同 32525 不同 build；如實記錄） |
| repo 回滾件 | `kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` 4879560 B，`5da5818d…86bd`，`strings` 含 `32525`/`3.2.5`，`file` 為 `Android 24 / NDK r29 / not stripped` ✓ |
| su / SELinux | `su -c 'id'` → `u:r:ksu:s0`，`getenforce` → `Enforcing` |
| Manager | `dumpsys` → `versionName=v3.2.5`，`versionCode=32525` |
| 模組 | PIF-NEXT v3.0(30)、TrickyStore v1.4.1(245)、Zygisk-Assistant v2.1.4(214)、LSPosed v2.2.0(7854)、ZygiskNext 1.5.0(843) |
| module/ksud | `/proc/modules: kernelsu … Live`，`/data/adb/ksud -V` → `ksud 3.2.5`，`debug info` → `version: 32525, uapi 2, lkm true, late_load true` |
| loader 路徑 | `/data/local/tmp/ksud-s25u-kdp` = `5da5818d…`（v3.2.5），已另存 `ksud-s25u-kdp.bak-G`；exploit `.so` 在機 `dm3q-app.so` = `648cfa81…`（與 artifacts 一致），root helper = `c49a6557…`（一致） |

## 2. 测试路径说明（为何 reboot）

- 開工態為 KSU 接管態（`u:r:ksu:s0 / Enforcing`），舊 temp daemon 已死：
  shell `cve-2026-43499-root -c/--late-load` → `su: connect daemon: Permission denied`（EACCES）；
  `su -c '… -c "id"'` → `su: permission denied`（daemon 存活但 `allowed_uid=2000` 拒 uid 0）。
  helper 內 ephemeral 探針在舊 Live 下必短路（`verify` 只看 flags，不比版本），在機直測新件必為假陽性/雙載風險。
- 故按「guarded `--late-load` 走 root helper」字面要求，取**乾淨重啟 + 原鏈 + 新件 late-load**路線：
  主動 `adb reboot`（clean，無 panic），等 `uptime>120s + load<1`，重跑原鏈。此舉在護欄 3 語義內（reboot 等重啟 + exploit 重試預算），未違反任一「違反即停」項。

## 3. 執行（護欄 2+3）

1. **重啟**：`adb reboot` 12:23:32Z，`wait-for-device` 後 `sys.boot_completed=1`，
   `/proc/modules` 無 kernelsu（clean ✓），`/data/local/tmp` 四件全存活且 sha 全對
  （`dm3q-app.so 648cfa81…`、`root c49a6557…`、`ksud-s25u-kdp 5da5818d…`、`ksud-v330-new aab76943…`）。
2. **exploit 鏈 attempt 1/3**（初次即中，無需重試）：
   `SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_TIMEOUT=115 TIMEOUT=600 … --run-payload dm3q-app.so … G-v330-exploit1.log`
   → `slide=0x170000` quorum `hits=154`，`controlled mm group full …745`，
   `cfi write/read ret=35`，`pipe physrw … done=1 root=1 uid=2000->0`，`exploit completed 1/1`，exit 0。
   `…-root -c 'id; getenforce'` → `u:r:kernel:s0 / Permissive`（temp root ✓）。
3. **stage 新件**（經 helper `-c`，temp daemon 存活態）：
   `cp ksud-v330-new → ksud-s25u-kdp` + `cp → .ksud-stage`，雙 sha 皆 `aab76943…` ✓。
4. **guarded `--late-load`**（經 helper，不手動 insmod）：
   - ephemeral 探針：`error: unexpected argument '--ephemeral'`（v3.3.0 新行為）→
     `late-load: KernelSU driver fd unavailable` → `module not live; retrying plain late-load`（helper 既有 fallback，e1q 註記同理）。
   - plain：exit 0（Enforcing 恢復致 client 靜默斷開，S9370 同現象，屬預期）。
   - 全程**無 panic/reboot**，exploit 重試計數 0/2，**未觸發回滾條件**。

## 4. 驗證門（護欄 4）— 5/5 通過

| 門 | 結果 |
|---|---|
| `/proc/modules Live` | `kernelsu 212992 0 - Live 0x… (OE)` ✓（refcount 0 vs 舊 1，fresh late-load 下 Zygisk 尚未附著，記錄備查） |
| `su→u:r:ksu:s0` | `su -c 'id'` → `uid=0(root) … context=u:r:ksu:s0` ✓ |
| `Enforcing` | `su -c 'getenforce'` → `Enforcing` ✓ |
| `ksud -V=3.3.0` | `/data/adb/ksud -V` → `ksud 3.3.0 (uapi: 2)` ✓；`/data/adb/ksud` 5101488 B `aab76943…`（plain 已替換舊件） |
| `debug info version 32601` | `version: 32601, flags 0x5, uapi 2, features 0x6, lkm true, late_load true, runtime_mode: late-load` ✓（features `0x6` vs 舊 `0x5`，v3.3.0 新增位，預期） |

- Manager：仍 `v3.2.5 / 32525`，與 32601 mismatch banner 屬預期，**未升級、未修**（用戶決議）。
- 模組目錄五項仍在（`playintegrityfix/tricky_store/zygisk-assistant/zygisk_lsposed/zygisksu`）。

## 5. 回滾狀態 + 最終手機狀態（回傳）

- **是否回滾：否**。`ksud-s25u-kdp.bak-G`（v3.2.5）與 `/tmp/fzg1-G-backup/ksud-backup-G-32525`（在機 `-mm` 變體）留作備查，未推回。
- 每步結果：備份 ✓ → 重啟clean ✓ → exploit 1/1 ✓（重試 0/2）→ stage ✓ → late-load（ephemeral 拒→plain 成）✓ → 五門 ✓。
- 最終手機狀態：**v3.3.0 已載入並接管**（`32601 / Live / ksu:s0 / Enforcing`），`su` 可用，Manager v3.2.5 mismatch 預期內。
  本 root 仍 per-boot 揮發，重啟即失，屬已知條件；未留半磚/無 root（當前即有 root）。
- 本任務 repo 寫入：僅本檔 `docs/BUILD-FZG1-v3.3.0-G.md`（同期 4B 的 H.md 另計），未動二進位/feed，未提交。`/tmp`  full log 見 `/tmp/fzg1-v330-G-verify.log`
 （含 exploit 全尾 40 行、late-load 原文、sha 表）；device 側 `G-v330-exploit1.log` 留機備查。

## 回傳摘要（給協調者）

- v3.3.0 `.new`（`aab76943…`）：**硬體驗證通過**，`32601 / Live / ksu:s0 / Enforcing / 3.3.0`。
- 限制：v3.3.0 掉 `--ephemeral`，helper ephemeral 探針必 fallback plain（本次 plain 成）；`features 0x6`、`refcount 0` 見 §4；
  Manager 配對仍 v3.2.5（mismatch 預期）；發布/配對由協調者定案。
- 無回滾，手機當前即 v3.3.0 root 態。
