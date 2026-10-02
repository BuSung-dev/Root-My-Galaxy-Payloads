# BUILD FZG1 v3.3.0 — H: ksud 內嵌證明 + S918B ELF 結論 + log 封存 (Stage 4 Worker-4B, host-side)

日期：2026-10-01｜分支：`dm3q-fzg1-v330-rebuild`（未切分支）｜工作區：`/mnt/240G_SSD/wt-fzg1-v330`
repo 新增：本次 4B 僅本檔 `docs/BUILD-FZG1-v3.3.0-H.md`（與 4A 的 G.md 合計共 2 個 untracked，tracked 零修改）；不提交；repo 內二進位與 feed 未動。
對象（二進位皆 repo 外，只讀未改）：
- `/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new`（5101488 B，`aab76943…fc940`）
- `/mnt/240G_SSD/wt-fzg1-kernel/kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko`（364584 B，`7ee01a06…49ac7b`）

> 分工：本檔只記 4B host-side 補證。`.ko` 見 E.md；ksud 重建見 F.md；ksu_props 見 D.md。
> 保證：未跑 adb shell 寫入（唯讀）；未改二進位/feed；未提交。

## 0. 結論先行

- **嵌入證明：有（閉環 F.md §4 / R3B 殘留缺口）**：`.new` 內 `0x30ace` 起 115818 B raw-DEFLATE 段，
  解壓得 364584 B，與正式 `.ko` **byte-identical**（`sha256 7ee01a06…49ac7b`，`cmp` 通過）。見 §1。
- **S918B-exact vmlinux：不可恢復（正式限制）**：`/mnt/240G_SSD` 下無 S918B 韌體/AP/boot/vmlinux；
  既有 AP tar 僅 S9180 FZG1/FZF5；host 僅 `vmlinux.elf`（FZG1）+ `vmlinux-fzf5.elf`。E.md proxy audit 維持原判，
  exact audit 待件到位。見 §2。
- **log 封存**：關鍵 log 清單見 §3（`.ko` 構建 log 已落檔；ksud cargo check/build 為終端輸出無落檔，F.md 內記 exit 0，
  本檔如實記錄不補造）。

## 1. Binary 內嵌證明（rust-embed payload 解壓抽取）

### 1.1 前提（源碼側，F.md §4 的「預期壓縮」落實）

- `ksu-S918B-v330/userspace/ksud/src/assets.rs:43-46`：aarch64-android 用
  `#[derive(RustEmbed)] #[folder = "bin/aarch64"]`；stage 資產即
  `bin/aarch64/android13-5.15_kernelsu.ko`（`/tmp/ksud-3B-v330` 殘留仍在，`sha256 7ee01a06…` 與正式 `.ko` 一致）。
- `Cargo.toml:14-17`：`rust-embed = { version = "8", features = ["debug-embed", "compression"] }`；
  本次鎖定（`/tmp/ksud-3B-v330/Cargo.lock`）：`rust-embed 8.12.0` + `include-flate 0.3.4` +
  `include-flate-compress 0.3.4` + `libflate 2.3.1`。
- `include-flate` 機制（registry 源碼已驗）：`codegen::deflate_file!` 把資產以 **raw DEFLATE**
  （`libflate::deflate::Encoder`，`apply_compression`）存為 `b"…"` 字面進 `.rodata`；
  無 zlib/gzip/zstd 魔數，故 `vermagic`/`5.15.189` 明文不可 grep（F.md §4 預期正確）。
  實測：`.new` 內 `vermagic abS916BXXSAFZG1` 出現 0 次，`5.15.189` 0 次，`RKP build` 0 次；
  資產檔名 `android13-5.15_kernelsu.ko` 明文 1 次（`0x15754a` 附近，Asset 索引表），與機制一致。

### 1.2 方法（逆向 asset 壓縮段，host-side 只讀）

1. 確認壓縮算法：`rust-embed 8.12 → include-flate (default deflate+zstd)`，
   未指定 `with zstd` 故走 `CompressionMethod::Deflate`（`compress` 預設分支已驗）；
   二進位內 `zstd magic 28b52ffd` 出現 0 次，佐證非 zstd。
2. 重壓對照（`/tmp/ksud-embed-proof`，`include-flate-compress =0.3.4` + `libflate =2.3.1`，
   與鎖定版同）：`apply_compression(ko, Deflate)` 得 115879 B，round-trip 解壓回 364584 B 與原 `.ko` 一致；
   但該 115879 B 在 `.new` 內 **0 命中**（連首 64/256/1024 B 前綴皆無），僅前 12 B
   `edfd0f609765bdff8fbfc7a60` 與實嵌段頭相同，第 13 B 起分叉。
   判讀：libflate 編碼器版本敏感（`libflate_lz77` 解析差異即可致數十 B 級分叉），重壓位元組比對**不可作 gate**，如實記錄。
3. 解壓掃描（決定性）：對 `.new` 全域（5101488 B）逐偏移試 `libflate::deflate::Decoder`
   raw-inflate（`scan.rs`），先讀 4 B 判 `7f454c46`（ELF），命中再全量解壓比對。
   結果唯一正解：**`0x30ace`（199374）** 解壓出 364584 B，與 `.ko` 全等（§1.3）。
   另兩處 `7f454c46` 為主 ELF（`0x0`，PIE TYPE 3）與內嵌 bootstrap ET_REL（`0x15ff5d`，TYPE 1，
   對應 `lkm_image.rs:1771` 節），非資產段。

### 1.3 證據（可重跑）

```sh
python3 -c "import zlib; data=open('/mnt/240G_SSD/wt-fzg1-kernel/ksud-dm3q-S918BXXSAFZF5-v3.3.0.new','rb').read();
off=0x30ace; d=zlib.decompressobj(-15); out=d.decompress(data[off:off+200000]);
print(len(out), len(data[off:off+200000])-len(d.unused_data), d.eof)"
# → 364584 115818 True
sha256sum /tmp/ksud-embed-proof/extracted.ko \
  /mnt/240G_SSD/wt-fzg1-kernel/kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko
# → 7ee01a065cd7f6743b2db2723620c8f6c4e61e0a050d98f541dc5cf79c49ac7b ×2
cmp /tmp/ksud-embed-proof/extracted.ko \
  /mnt/240G_SSD/wt-fzg1-kernel/kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko && echo CMP-IDENTICAL
```

| 項 | 值 |
|---|---|
| 內嵌段偏移 | `0x30ace`（199374） |
| 內嵌段壓縮長 | **115818 B**（consumed；`off..off+200000` 試解，`unused_data` 84182） |
| 段頭 32 B | `edfd0f609765bdff8fbfc7a60e8402198a4a318d6a12ea8001c32887624da524`（段前 32 B 為 `…6c6f636b00…766d616c6c6f635f6e6f70726f66`，rodata 上下文正常） |
| 解壓長 | 364584 B |
| 解壓 SHA-256 | `7ee01a065cd7f6743b2db2723620c8f6c4e61e0a050d98f541dc5cf79c49ac7b` = 正式 `.ko`（`cmp` 全等） |
| 重壓對照 | 同庫重壓 115879 B（+61 B 差，首 12 B 同），二进制內 0 命中 → 不作 gate，僅記方法阻塞（編碼器版本敏感） |
| 工具殘留（repo 外） | `/tmp/ksud-embed-proof/{Cargo.toml,src/main.rs,src/bin/scan.rs,recompressed.bin,extracted.ko}`（`extracted.ko` 即解壓物，364584 B） |

- **判定：嵌入證明有。F.md §4「內嵌 `.ko` 未解壓驗證」殘留缺口（R3B）就此閉環**：
  `.new` 內嵌的確為 3A 正式 S918B 資產（`7ee01a06…`，vermagic `abS916BXXSAFZG1`），非 smoke 件。
- 未動項：`.new`/`.ko` 皆只讀；`/tmp/ksud-embed-proof` 為本次逆向工具（repo 外，可刪）。

## 2. S918B-exact vmlinux（能否從既有材料恢復）

### 2.1 搜索範圍（`/mnt/240G_SSD`，host-side 只讀）

- `*vmlinux*`（maxdepth 3）：僅 `s9180-fzg1/vmlinux.elf`（51M，FZG1）、
  `s9180-fzf5/vmlinux-fzf5.elf`、`s9210-dzg1/analysis/vmlinux.*`；**無 `*918B*`/`*916B*` vmlinux**。
- `*918B*`/`*916B*`（maxdepth 2）：僅 `wt-fzg1-kernel/{kernelsu-dm3q-S918BXXSAFZF5-v3.3.0.ko,
  ksud-dm3q-S918BXXSAFZF5-v3.3.0.new,ksu-S918B-v330}`（本次構建產物/樹）+
  `wt-fzg1-v330/{artifacts,kernelsu,src/targets,docs}` 下既有檔；**無韌體包、無 boot、無源包**。
- AP tar：僅 `s9180-fzg1/AP_S9180ZHS8FZG1…FZG1…tar.md5`（13G）+
  `s9180-fzf5/AP_S9180ZHS8FZF5…FZF5…tar.md5`（13G）+ BL（FZG1 119M）；**AP 即 FZG1（S9180），無 S918B 韌體**。
- `/mnt/240G_SSD/boot.img`（64M）：`file = Android bootimg`，全檔 `strings` 無
  `abS9/S918/33413713` 明文（gzip 內核壓縮態，無法由此得 S918B exact ELF；且歸屬未證為 S918B，不可強認）。
- `s9180-fzg1/boot.img` 已驗為 FZG1（`strings` 出 `5.15.189…abS9180ZHS8FZG1`，見 §2.2 對照）。

### 2.2 結論：不可恢復（正式限制）

- **S918B-exact（`abS918BXXSAFZF5`）vmlinux/boot 不在 host 上，無材料可恢復**（AP 是 FZG1 的，
  S918B 韌體從未入 host）。E.md §5.1「S918B-exact vmlinux 缺」維持，升級為**正式限制**：
  S918B-exact `check_symbol`/`audit_module_against_target.py` 無法在 host 補跑；
  E.md §4 以 FZG1 `vmlinux.elf` 作 proxy 的 audit（`undefined 200 / missing 0 / CRC mismatches 0 / exit 0`）為當前最終 gate。
- 後續（Stage 4+）：S918B 真機 `boot.img` 到位後，解包取 `Image`→`vmlinux`（或廠包 `vmlinux.elf`），
  再對本 `.ko`（`7ee01a06…`）重跑 exact audit；此為真機側任務，host-side 到此為止。

## 3. 關鍵構建 log 封存清單（拷貝清單）

> 本次不新建 repo 內 log 檔；僅把既有 log 位置+hash 記入（「拷貝清單」）。
> ksud `cargo check --target` / `cargo ndk --platform 26 build` 為 F.md 終端輸出（exit 0，唯一 warning
> `utils.rs:360 daemonize never used`），**未落檔**——此處如實記錄，不補造 log。
> 若後續需落檔重跑，請在 `/tmp` 隔離重跑並 `tee` 存檔（勿動 repo）。

| # | log（原位，只讀） | 內容 | 大小/hash |
|---|---|---|---|
| L1 | `/mnt/240G_SSD/wt-fzg1-kernel/logs/kernelsu-ko-build-S918B-r25c.log` | S918B v3.3.0 `.ko` 構建（E.md §3，exit 0，0 error） | 9.7K，`sha256 d4494229…9854cf` |
| L2 | `/mnt/240G_SSD/wt-fzg1-kernel/logs/kernelsu-ko-build-r25c.log` | FZG1 v3.3.0 `.ko` 構建（C.md 對照） | 9.4K，`sha256 4d4147d8…90d70d6` |
| L3 | `/mnt/240G_SSD/wt-fzg1-kernel/logs/modules_prepare-r25c.log` | headers 備妥（r25c） | 46K |
| L4 | `/mnt/240G_SSD/wt-fzg1-kernel/logs/olddefconfig.log` + `diffconfig.txt` + `live.config` | config 鏈（A.md） | 554 B / 13K / 189K |
| L5 | `/tmp/dm3q-kernel-verify/audit-fzg1.log` | audit 參照輸出（6 行：`undefined 200 / version 0 / missing 0 / kallsyms 64 / without-CRC 200 / mismatches 0`） | 228 B |
| L6 | `/tmp/phaseC_{audit,check,modinfo,readelf_SW,file,md5,sha,sizes}_*.log` | Phase-C 三態比對（ref/unstrip/strip） | 各 <1K（`audit_unstrip` 228 B：`undefined 202…` 系 FZG1 側舊數，E.md S918B 版以 §4 表為準） |
| L7 | ksud cargo 鏈（**無落檔**） | `cargo fetch` exit 0；`cargo check --target aarch64-linux-android` exit 0；`cargo ndk -t arm64-v8a --platform 26 build --release` exit 0（24.07s，F.md §2-§3） | 終端輸出，見 F.md §1-§3 行內記錄 |
| L8 | 本次 4B 逆向工具輸出 | `/tmp/ksud-embed-proof/extracted.ko`（364584 B，`7ee01a06…`）+ `recompressed.bin`（115879 B）+ `scan` 終端（`EXACT MATCH at 0x30ace`） | repo 外暫存，可刪；重跑命令見 §1.3 |

## 回傳摘要（給協調者）

- **嵌入證明：有**。`.new(0x30ace,115818B)` → raw-DEFLATE → `7ee01a06…49ac7b`（364584 B，`cmp` 全等）。
  R3B（F.md §4 內嵌未解壓驗證）殘留缺口**閉環**；內嵌物 = 3A 正式 S918B 資產，非 smoke。
- **S918B ELF 結論：不可恢復（正式限制）**。`/mnt/240G_SSD` 無 S918B 韌體/AP/boot/vmlinux；
  AP tar 僅 S9180 FZG1/FZF5；exact audit 待真機件，proxy（FZG1）維持最終 gate。
- 封存：`.ko` 構建/audit log 清單如 §3；ksud cargo 鏈無落檔（F.md 行內為準），未補造。
- 保證：工作區分支仍 `dm3q-fzg1-v330-rebuild`；`git status` 為本檔 + G.md 共 2 個 untracked（tracked 零修改）；二進位/feed 未動；未提交。
