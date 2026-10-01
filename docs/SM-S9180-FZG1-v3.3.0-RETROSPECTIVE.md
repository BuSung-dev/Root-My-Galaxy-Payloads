# SM-S9180 FZG1 v3.3.0 升級回顧：過程、數據與經驗

目標：Galaxy S23 Ultra `SM-S9180`（`S9180ZHS8FZG1`，核心
`5.15.189-android13-8-33413713-abS9180ZHS8FZG1`）的 KernelSU 從 v3.2.5
（32525）完整 rebase 到上游 v3.3.0（`932014a`，32601），並在真機驗證。

結果：真機 v3.3.0 working root（`Live` + `su → u:r:ksu:s0` + `Enforcing` +
`ksud 3.3.0` + `debug 32601`），可重複；發布暫緩（feed 維持 v3.2.5，見 §5）。

## 1. 流程（分支隔離 + 分階段 + 每階段 review）

分支 `dm3q-fzg1-v330-rebuild`（worktree 隔離），每階段 workers 產出經
review（記錄見各 commit message 與 E/F/G 後補 trace）→ fix→commit，
收尾經終審確認後 FF-merge 回 `main`：

| 階段 | 內容 | Review 結果 |
|---|---|---|
| Phase 0 | 前期調查（源樹／工具鏈／產物基線） | 事實確認 |
| Stage 1 | HKTW_16 源樹解包、config 拼裝、`modules_prepare`、工具鏈缺口盤點 | 2 reviewers 事實 PASS＋放行條件 → 修文件 → 確認 PASS |
| Stage 2 | NDK r25c、獨立 FZG1 `.ko` 構建＋審計、ksu_props 斷鏈解法 | 雙 PASS＋發布裁決（複用正線）→ 修文件 → 確認 PASS |
| Stage 3 | S918B 資產 `.ko`＋ksud v3.3.0 重建 | 有條件 PASS → 用語修正 |
| Stage 4 | 真機 single-cycle＋嵌入證明 | 雙 PASS（附發布條件）→ 用語修正 |
| Stage 4b | 可重複性＋回滾演練 | PASS＋發布裁決：暫緩 |
| 終審 | 收尾確認後 FF-merge＋push | 通過 |
| 實測升級 | 用戶手機直升 v3.3.0，1 試即中 | 5/5，終態 v3.3.0 |

全程文檔：`docs/BUILD-FZG1-v3.3.0-A.md`～`J.md` ＋ `live-FZG1.config`
（7112 行，取自已 root 手機 `/proc/config.gz`）。

## 2. 關鍵技術數據

- 源樹：`SM-S9180_HKTW_16_Opensource.zip`（GKI 式 Kalama，無 dm3q
  defconfig，以 `prepare_vendor.sh sec gki` 拼裝）；源樹 5.15.178 vs 目標
  5.15.189（＋11 drift）由 `audit --manual-relocation` 客觀裁決通過
  （`undefined 200 / version 0 / missing 0 / CRC 0`，另 `kallsyms 64`），
  無需 exact 189 樹。
- 編譯器：NDK r25c（clang 14.0.7，LLVM commit 與手機 `r450784e` 一致），
  LTO_FULL＋CFI＋MODVERSIONS 下非它不可（r27/28/29 跨大版不行）。
- 模組：S918B 資產 `vermagic abS916BXXSAFZG1` 精確、`__versions` size 0、
  無 `stop_machine`；ksud `5101488 B`（`aab76943…`），內嵌資產經解壓比對
  證明全等（`7ee01a06…`）。
- 真機：exploit＋late-load 全程無需新漏洞；v3.3.0 拒 `--ephemeral`
 （`unexpected argument`），fallback plain 即成；`features 0x5→0x6`。
- 波動：v3.3.0 三次首試中失敗 1 次（1 panic＋2 成功），v3.2.5 回滾輪
  首試亦失敗 1 次後重試成功，屬鏈路已知波動（樣本小，僅記錄現象），
  非 v3.3.0 特有。

## 3. 決策記錄

1. **源包選擇**：銷售碼 `BRI`（台灣）→ 同地區 `HKTW_16` 包；官方 ROM
   無源碼不能用，但已 root 手機可直接提供 live config＋audit 參照。
2. **FZG1 複用 S918B loader**：同 `33413713` family、kallsyms manual loader
   旁路 vermagic 後綴差；S918B 件有硬體驗證真值；獨立 FZG1 `.ko`
   僅作構建證明，不進 repo／feed。
3. **ksu_props 斷鏈**：整 repo 404，改用父庫 mirror 同 rev
  （commit hash 相等即內容相等，已核對），降級舊 rev 實測編不過。
4. **ksud `--platform 26`**：v3.3.0 新符號需 API 26+（上游 CI 亦用 26），
   不改回 24；FZG1 為 Android 13/14 世代（API 33/34），遠高於 26，運行無礙。
5. **發布暫緩**：`--ephemeral` 移除＋Manager 配對（核心 32601／Manager
   32525）未定案前不進 feed；手機實測不受影響。

## 4. 經驗教訓

- **版本字串覆寫 ≠ ABI 保證**：`kernel.release` 覆寫只解決 vermagic，
  真正的 gate 永遠是 `check_symbol`＋`audit`＋真機 late-load。
- **stub 只能證工具鏈**：空 whitelist 跑通 `modules_prepare` 不代表裁剪語義
  正確，嚴禁流入正式 `.ko`。
- **先查在機基線**：手機裡的舊 ksud（`4556352B 077797b6… -mm` fork 變體）
  與 repo 回滾件（S918B `5da5818d`）同版本不同 build，
  「原地升級」的敘述必須先核對 sha，否則回滾會回到錯的基線。
- **版本號三件套要同動**：`KSU_VERSION`／`VERSION_CODE`／Manager 必須一起看，
  單升一處就是 mismatch。
- **`--ephemeral` 這類參數移除是 breaking change**：helper 與文件假設要同步修，
  不能只靠 fallback。
- **git 依賴 pin rev 也會斷**：上游 repo 消失比想像中常見，mirror 以 hash 驗證為準。

## 5. 待辦（v3.3.0 正式發布前）

1. 修 root helper／文件：`--ephemeral` 探針改 fallback 語義。
2. Manager 配對策略：建議同步升 v3.3.0（32601）消除 mismatch。
3. S918B-exact vmlinux：待取得 S918B 韌體後補 exact audit（現以 FZG1 proxy＋真機為 gate）。
4. 屆時更新 `kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp`＋`support/targets-v3.json`
   尺寸，並重跑一次真機全鏈。
