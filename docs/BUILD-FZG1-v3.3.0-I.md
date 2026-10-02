# BUILD FZG1 v3.3.0 — I: 可重複性 + 回滾演練 (Stage 4b Worker)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切）｜工作區：`/mnt/240G_SSD/wt-fzg1-v330`
手機：R5CW21C8PLR (SM-S9180 FZG1)｜repo 寫入：僅本檔 `docs/BUILD-FZG1-v3.3.0-I.md`；未提交；二進位/feed 未動
對象（推前皆 sha256sum 比對，repo 外只讀未改）：
- v3.3.0：`/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new` 5101488 B，`aab76943…fc940` ✓
- v3.2.5：`kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` 4879560 B，`5da5818d…86bd` ✓
- 原鏈：`dm3q-app.so 648cfa81…` + `cve-2026-43499-root c49a6557…`（在機/artifact 一致）
- 環境（G 原鏈落實）：`SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600`（源碼 `preload.c:130-138` 全名；短名 `P0_TIMEOUT/TIMEOUT` 走預設 1200/2200，不可作 gate）
- 護欄：guarded `--late-load` 經 root helper（不手動 insmod）；Manager 保持 v3.2.5 不升級；exploit 每輪重試 ≤2；panic 等重啟記樣本，重複 panic 即停

## 1. 第一輪：可重複性 v3.3.0（通過，2 試：1 panic + 1 成功）

1. 開工態 v3.3.0 Live（`32601 / ksu:s0 / Enforcing / 3.3.0`）→ `adb reboot` 13:26:55Z，`boot_completed=1`，`up 0 min`
2. clean ✓：`/proc/modules` 無 kernelsu（`grep -c = 0`）；四件全存活且 sha 全對（`dm3q-app 648cfa81 / root c49a6557 / ksud-s25u-kdp aab76943 / ksud-v330-new aab76943`，註：s25u-kdp 已是 G 輪 stage 後的 v330 殘留）
3. 安靜窗口：uptime>120s + load<1（約 10 min，`0.02~0.09`）
4. exploit attempt 1/3（誤用短名，`p0_timeout=1200 timeout=2200`，slide `000a0000 hits=154`）：卡在 `durable log checkpoint stage=fops-page-held` 後掉線 → 等重啟（transport 609→610，`up 23s`）→ **panic 樣本 #1**：重啟後 `NO-KSU-CLEAN`，pstore shell 側 `Permission denied`（無 root 不可讀，如實記），在機 log 止於 fops-page-held（`/tmp/I-v330-exploit1-panic.log` 8180 B）。非重複 panic，繼續
5. exploit attempt 2/3（修正全名，`p0_timeout=115 timeout=600`，slide `00050000 hits=164`）：`exploit completed 1/1` exit 0，`pipe physrw done=1 root=1 uid=2000->0`，helper `-c` → `u:r:kernel:s0 / Permissive`（temp root ✓）
6. stage（經 helper `-c`）：`ksud-v330-new → ksud-s25u-kdp + .ksud-stage`，雙 sha `aab76943…` ✓
7. guarded `--late-load`：`unexpected argument '--ephemeral'` → `module not live; retrying plain late-load` → exit 0（與 G 同行為，plain 成；無 panic）

| 門 | 結果 |
|---|---|
| `/proc/modules Live` | `kernelsu 212992 0 - Live (OE)` ✓ |
| `su→u:r:ksu:s0` | `uid=0 … context=u:r:ksu:s0` ✓ |
| `Enforcing` | `Enforcing` ✓ |
| `ksud -V=3.3.0` | `ksud 3.3.0 (uapi: 2)`，`/data/adb/ksud aab76943…` ✓ |
| `debug info 32601` | `version: 32601, features 0x6, lkm true, late_load true, runtime_mode: late-load` ✓ |

Manager 仍 `v3.2.5/32525`（mismatch 預期，未動）；模組五項全在。

## 2. 第二輪：回滾演練 v3.2.5（通過，2 試：1 乾淨失敗 + 1 成功；終態 v3.2.5 working root）

1. 推前比對 ✓（`5da5818d…86bd`）→ `adb reboot`，clean ✓（`NO-KSU-CLEAN-R2`，`up 0 min`）
2. `adb push kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp → /data/local/tmp/ksud-rollback-v325`，在機 sha `5da5818d…` ✓；`dm3q-app/root` sha 照舊
3. 安靜窗口：本次開機異常高負載（1-min 峰值 30+），等 ~18 min 至 `up 18 min / load 0.17` 才開工（如實記）
4. exploit attempt 1/3（全名正確，slide `00108000 hits=157`）：`root umh prepublish write failed … stack writer ran; refusing retry on this boot`，`EXPLOIT_RC=255` **乾淨失敗（非 panic）**；隨後硬重置重啟（`up 5 min`，transport →627），重啟後 clean，rollback 件 sha 仍對。one-shot 已耗， reboot 後重試符合 profile 語義
5. exploit attempt 2/3（slide `000f0000 hits=159`）：`exploit completed 1/1`，`uid=2000->0`，helper `-c` → `u:r:kernel:s0 / Permissive` ✓
6. stage 回滾件（經 helper `-c`）：`ksud-rollback-v325 → ksud-s25u-kdp + .ksud-stage`，雙 sha `5da5818d…` ✓
7. guarded `--late-load`：同樣先 `--ephemeral` 被拒再 plain 成，exit 0

| 門 | 結果 |
|---|---|
| `/proc/modules Live` | `kernelsu 208896 0 - Live (OE)` ✓（尺寸回 208896，與 G 開工態一致） |
| `su→u:r:ksu:s0` | ✓ |
| `Enforcing` | ✓ |
| `ksud -V=3.2.5` | `ksud 3.2.5`，`/data/adb/ksud 5da5818d…`（= repo 件） ✓ |
| `debug info 32525` | `version: 32525, features 0x5, lkm true, late_load true` ✓（舊版無 `runtime_mode` 行，屬版本輸出差異） |

Manager `v3.2.5/32525` 與 ksud 配對一致；模組五項全在；**終態 = v3.2.5 working root（用戶選定穩態）**，絕無 Manager 升級。

## 3. Log 封存（/tmp）

- `/tmp/fzg1-v330-I-round1.log`（主 log，含 reboot/clean/sha/兩試/late-load/五門）
- `/tmp/fzg1-v330-I-round2.log`（同上，回滾輪）
- `/tmp/I-v330-exploit1-panic.log`（8180 B，panic 樣本）+ `/tmp/I-v330-exploit2.log`（11958 B，成功）
- `/tmp/I-v325-exploit1-fail.log`（13982 B，乾淨失敗 rc255）+ `/tmp/I-v325-exploit2.log`（11844 B，成功）
- 在機：`/data/local/tmp/I-v330-exploit{1,2}.log`、`/data/local/tmp/I-v325-exploit{1,2}.log`

## 回傳摘要

- 第一輪（v3.3.0 可重複性）：**通過**（5/5：`Live / ksu:s0 / Enforcing / 3.3.0 / 32601`）。代價：2 試（1 panic 在 fops-page-held 後 + 1 成功）；panic 未重複。附帶確認：env 必須用全名（`P0_ATTEMPT_TIMEOUT_SEC / EXPLOIT_ATTEMPT_TIMEOUT_SEC`），短名走預設；v3.3.0 拒 `--ephemeral`，helper fallback plain 成
- 第二輪（回滾 v3.2.5）：**通過**（5/5 回到 `32525 / 3.2.5 / ksu:s0 / Enforcing / Live`，`/data/adb/ksud = 5da5818d…`）。代價：2 試（1 乾淨失敗 rc255 + 硬重置 + 1 成功）；無 panic
- 終態：**v3.2.5 working root**（`208896 Live / ksu:s0 / Enforcing / 3.2.5 / 32525`，Manager v3.2.5 配對，模組 5/5），符合用戶穩態要求
- 發布門檻：**未達**。v3.3.0 功能可重現但：(a) 需容忍 panic/乾淨失敗重試（本輪 50% 首試失敗；README 已載明 expect retries）；(b) `--ephemeral` 被拒，helper/文件若假定其存在需更新；(c) Manager 配對仍 v3.2.5（mismatch 預期內，用戶決議）；(d) features `0x6` vs `0x5`、refcount 差異僅記錄。建議維持 **v3.2.5 穩態**，v3.3.0 待更多首試統計 + ephemeral/Manager 配對定案後再議
- 保證：分支仍 `dm3q-fzg1-v330-rebuild`；repo 僅本檔新增（tracked 零修改，未提交）；Manager 未升級；二進位/feed 未動
