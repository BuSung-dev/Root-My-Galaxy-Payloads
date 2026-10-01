# BUILD FZG1 v3.3.0 — J: 升級實測 v3.2.5 → v3.3.0 (Stage 5 Worker)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切）｜工作區：`/mnt/240G_SSD/wt-fzg1-v330`
手機：R5CW21C8PLR (SM-S9180 FZG1)｜repo 寫入：僅本檔 `docs/BUILD-FZG1-v3.3.0-J.md`；未提交；二進位/feed 未動
對象（推前皆 sha256sum 比對，repo 外只讀未改，覆寫僅 /data/local/tmp）：
- v3.3.0：`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new` 5101488 B，`aab76943…fc940` ✓
- v3.2.5 回滾件：`kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` 4879560 B，`5da5818d…86bd` ✓
- 原鏈：`artifacts/dm3q-S9180ZHS8FZG1/cve-2026-43499-app.so 648cfa81…` + `cve-2026-43499-root c49a6557…`（在機一致）
- 環境（全名，I 輪教訓）：`SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600`
- 護欄：guarded `--late-load` 經 root helper（不手動 insmod）；Manager 保持 v3.2.5 不動；exploit 每輪重試 ≤2；連續 panic/失敗即停並回滾；成功則停 v3.3.0 不回滾

## 1. 開工態 v3.2.5 working root（護欄 1）— 記錄

| 項 | 結果 |
|---|---|
| `su -c id` / SELinux | `uid=0 … context=u:r:ksu:s0` / `Enforcing` |
| `/proc/modules` | `kernelsu 208896 1 - Live (OE)` |
| `ksud -V` / `debug info` | `ksud 3.2.5` / `version: 32525, features 0x5, lkm true, late_load true`（舊版無 `runtime_mode`） |
| `/data/adb/ksud` | 4556352 B，`077797b6…215eb57`（在機 `-mm` 變體，與 repo kdp `5da5818d…` 同 32525 不同 build；已備份 `/data/local/tmp/ksud-backup-J-32525` → `/tmp/fzg1-J-backup/`，如實記） |
| Manager | `versionName=v3.2.5 versionCode=32525` |
| 模組 | 5/5：`playintegrityfix/tricky_store/zygisk-assistant/zygisk_lsposed/zygisksu` |
| loader 路徑 | `/data/local/tmp/ksud-s25u-kdp 5da5818d…`（canonical v3.2.5），`/data/local/tmp/ksud-v330-new aab76943…`，`dm3q-app.so 648cfa81…`，`root c49a6557…` 全對 |
| uptime | `up 1:15 / load 1.74`（開工態高負載，reboot 後重取安靜窗口） |

## 2. 執行（護欄 2+3）— 1 試即中，無 panic，無重試

1. **推前比對** ✓（`.new aab76943…`）→ `adb push … → /data/local/tmp/ksud-v330-new`，在機 sha `aab76943…` ✓
2. **reboot** 15:42:08Z → `wait-for-device` 15:42:42Z，`boot_completed=1`，`up 0 min`，transport 681
3. **clean** ✓：`grep -c kernelsu = 0`（`NO-KSU-CLEAN`）；四件全存活且 sha 全對（`dm3q-app 648cfa81 / root c49a6557 / s25u 5da5818d / v330-new aab76943`）
4. **安靜窗口**：`uptime>=120s + load1<1`，約 2 min（`up 2 min / load 0.50`，15:44:25Z）
5. **exploit attempt 1/2**（全名正確，`p0_timeout=115 timeout=600`，slide `001a8000 hits=163`）：`exploit completed 1/1` exit 0，`pipe physrw done=1 root=1 uid=2000->0`（`fresh physrw retry page attempt=2/12` 後命中 `idx=11`，屬正常重試），helper `-c` → `u:r:kernel:s0 / Permissive`（temp root ✓）。重試計數 0/2，**未觸發回滾條件**
6. **stage**（經 helper `-c`）：`ksud-v330-new → ksud-s25u-kdp + .ksud-stage`，雙 sha `aab76943…` ✓
7. **guarded `--late-load`**（經 helper）：`unexpected argument '--ephemeral'` → `module not live; retrying plain late-load` → exit 0（與 G/I 同行為，plain 成；無 panic）

## 3. 驗證門（護欄 4）— 5/5 通過

| 門 | 結果 |
|---|---|
| `/proc/modules Live` | `kernelsu 212992 0 - Live (OE)` ✓ |
| `su→u:r:ksu:s0` | `uid=0 … context=u:r:ksu:s0` ✓ |
| `Enforcing` | `Enforcing` ✓ |
| `ksud -V=3.3.0` | `ksud 3.3.0 (uapi: 2)`，`/data/adb/ksud aab76943…` 5101488 B ✓ |
| `debug info 32601` | `version: 32601, features 0x6, lkm true, late_load true, runtime_mode: late-load` ✓ |

- Manager 仍 `v3.2.5/32525`（mismatch 預期，未動）；模組五項全在。
- 是否回滾：**否**（成功則終態停 v3.3.0，不回滾；回滾件未推回，備份留作備查）。

## 4. Log 封存（/tmp，只寫 J.md + /tmp log）

- `/tmp/fzg1-v330-J-verify.log`（主 log，含開工/reboot/clean/安靜/一試/stage/late-load/五門）
- `/tmp/J-v330-exploit1.log`（13719 B，成功；host pull）在機：`/data/local/tmp/J-v330-exploit1.log`
- 備份：`/tmp/fzg1-J-backup/ksud-backup-J-32525`（4556352 B，`077797b6…` 在機 -mm 件）+ 在機 `/data/local/tmp/ksud-backup-J-32525`

## 回傳摘要

- **結果：通過**（5/5：`Live / ksu:s0 / Enforcing / 3.3.0 / 32601`）。代價：1 試（0 panic，0 乾淨失敗，0 重試）；`--ephemeral` 被拒走 plain 屬預期；`features 0x6`、`212992` 尺寸與 G/I 一致
- **終態：v3.3.0 working root**（`212992 Live / ksu:s0 / Enforcing / 3.3.0 / 32601`，`/data/adb/ksud = aab76943…`，Manager v3.2.5 mismatch 只記，模組 5/5），**未回滾**，當前即有 root（per-boot 揮發，重啟即失，屬已知條件）
- **log 位置**：`/tmp/fzg1-v330-J-verify.log` + `/tmp/J-v330-exploit1.log`（在機同名留存）
- 保證：分支仍 `dm3q-fzg1-v330-rebuild`；repo 僅本檔新增（tracked 零修改，未提交）；Manager 未升級；二進位/feed 未動
