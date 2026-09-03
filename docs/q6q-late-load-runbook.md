# q6q KernelSU late-load 璁惧楠岃瘉 Runbook

鍦ㄥ嚭宸瑪璁版湰涓婏紙Windows锛屽凡瑁?adb锛夊 SM-F9560锛堝簭鍒楀彿 <serial-elided>锛夊畬鎴?KernelSU late-load 楠岃瘉銆傞€愭潯鎵ц锛屾妸姣忔鐨勮緭鍑哄洖浼犮€?
## 0. 鍑嗗锛堢瑪璁版湰渚э級

1. 纭 adb 鍙敤锛歚adb devices` 鑳界湅鍒?`<你的设备序列号>  device`銆?2. 浠庝富宸ヤ綔鏈烘嬁 **3 涓枃浠?*锛堝叡绾?4.9MB锛屼换浣曚紶杈撴柟寮忓潎鍙級锛?
   | 鏂囦欢锛堜富宸ヤ綔鏈鸿矾寰勶級 | 澶у皬 | SHA-256 |
   | --- | ---: | --- |
   | `kernelsu/ksud-q6q-F9560ZCU3DZDP-kdp` | 4,767,984 | `968294F4638F9A77FD9C60B26D8E85B63E9B714D34035386E0A75D16BF465F47` |
   | `build/q6q-F9560ZCU3DZDP-v21/cve-2026-43499-app.so` | 137,680 | `3BD0395C8552FE3355369F2F4D7C01F386F14DCB6D423A87D8402A389D45B40B` |
   | `build/q6q-F9560ZCU3DZDP-v21/cve-2026-43499-root` | 26,712 | `6E1FE2D2C1BA16E990BD05262A8FEA4C65A1C165F8EF76A498696FEAA344417B` |

   璇存槑锛氳澶囦笂**鍙兘**杩樼暀鐫€涓婃 push 鐨?`f9560-v21-*`锛坴21 娴嬭瘯鏃舵帹鐨勶紝
   閭ｈ疆杩愯鎴愬姛娌℃湁宕╂簝锛夛紝浣嗕富宸ヤ綔鏈烘鍚庢棤娉曠‘璁よ澶囩姸鎬佲€斺€旀墍浠ヤ笁涓枃浠?   鍏ㄩ儴閲嶆柊 push 鏈€淇濋櫓銆?3. 鍙€変絾寤鸿锛氬厛璁板綍褰撳墠 boot_id
   `adb shell cat /proc/sys/kernel/random/boot_id`銆?
## 1. 鎻愭潈锛堜粎鍦?root 宸插け鏁堟椂闇€瑕侊級

**閲嶈鍒嗘敮锛氬鏋滀綘鎷垮埌鎵嬫満鍚庝竴鐩存病閲嶅惎杩囷紝涓婁竴杞?v21 鐨?root 澶ф鐜囪繕娲荤潃锛?姝ゆ椂鍗冧竾涓嶈閲嶈窇鎻愭潈**锛堝悓涓€ boot 涓?P0 浼氳瘽鏄崟娆＄殑锛岄噸璺戜細琚嫆缁濓級銆?鍏堣窇杩欐潯 5 绉掗獙璇侊細

```bat
adb shell "cat /proc/sys/kernel/random/boot_id; getenforce; ls -l /data/local/tmp/ | grep f9560; /data/local/tmp/f9560-v21-root -c 'id'"
```

鍒ゅ畾锛?- boot_id = `1f2a3f7b-59f0-40d5-b8dd-88036f8e1744`锛坴21 閭ｆ鐨?boot锛夈€?  `getenforce` = Permissive銆乣ls` 鑳藉垪鍑?`f9560-v21-*`銆乣id` =
  `uid=0(root) ... context=u:r:kernel:s0`
  鈫?**root 杩樻椿鐫€锛氳烦杩囨湰姝ワ紝鐩存帴鍋氱 2 姝?late-load**銆?- 浠讳綍涓€椤逛笉绗︼紙boot_id 鍙樹簡 / Enforcing / 鏂囦欢娌′簡 / su 鎶ラ敊锛?  鈫?root 宸插け鏁堬紝鎵ц涓嬮潰鐨勫畬鏁存彁鏉冦€?
```bat
adb push ksud-q6q-F9560ZCU3DZDP-kdp /data/local/tmp/ksud-q6q
adb push cve-2026-43499-app.so /data/local/tmp/f9560-v21-app.so
adb push cve-2026-43499-root /data/local/tmp/f9560-v21-root
adb shell "chmod 755 /data/local/tmp/ksud-q6q /data/local/tmp/f9560-v21-app.so /data/local/tmp/f9560-v21-root"
adb shell "cd /data/local/tmp && ./f9560-v21-root --run-payload ./f9560-v21-app.so /data/local/tmp/f9560-v21-root /data/local/tmp/f9560-v21-run2.log"
```

棰勬湡锛堢害 1-3 鍒嗛挓锛夛細鏃ュ織鏈€鍚庡嚭鐜?`pipe-physrw-summary ... done=1 root=1 kaslr=1 ... uid=2000->0` 涓斿懡浠ゆ甯搁€€鍑恒€?娉ㄦ剰锛氭彁鏉冩湡闂村唴鏍稿彲鑳?panic锛堣澶囪嚜鍔ㄩ噸鍚級鈥斺€旇嫢鍙戠敓锛岀瓑 `adb wait-for-device`
鍚庝粠澶撮噸璺戞湰姝ワ紙boot_id 浼氬彉锛屾甯革級銆傝窇瀹岀‘璁わ細

```bat
adb shell "getenforce; /data/local/tmp/f9560-v21-root -c 'id'"
```

棰勬湡 `Permissive` + `uid=0(root) ... context=u:r:kernel:s0`銆?
## 2. KernelSU late-load

```bat
adb shell "/data/local/tmp/f9560-v21-root -c 'echo 1 > /proc/sys/kernel/kptr_restrict'"
adb shell "cp /data/local/tmp/ksud-q6q /data/local/tmp/.ksud-stage && chmod 755 /data/local/tmp/.ksud-stage"
adb shell "cp /data/local/tmp/ksud-q6q /data/local/tmp/ksud-s25u-kdp && chmod 755 /data/local/tmp/ksud-s25u-kdp"
adb shell "/data/local/tmp/f9560-v21-root --late-load"
```

璇存槑锛歚--late-load` 鎴愬姛鏃堕潤榛橈紙daemon 浼氭仮澶?Enforcing锛屽鎴风闅忎箣鏂紑锛夈€?娉ㄦ剰 daemon 鐨勬寕杞借矾寰勫啓姝讳负 `/data/local/tmp/ksud-s25u-kdp`锛堝巻鍙查仐鐣欑殑
S25U 鍛藉悕锛寁21 helper 閲岀殑甯搁噺锛夛紝鎵€浠?*涓や釜鍚嶅瓧閮借澶嶅埗**銆?`.ksud-stage` 姣忔 late-load 鍓嶅繀椤婚噸寤猴紙loader 浼氭妸瀹冩敼鍚嶅埌 `/data/adb/ksud`锛夈€?鑻?late-load 鍚庤澶囬噸鍚?榛戝睆锛氳繖鏄ā鍧楀姞杞藉け璐モ€斺€旇褰?`adb shell ls /data/log/dumpstate_lastkmsg*` 鐨勬渶鏂版枃浠跺苟鍥炰紶銆?
## 3. 楠岃瘉

```bat
adb shell "/data/local/tmp/f9560-v21-root -c 'cat /proc/modules | grep kernelsu'"
```

棰勬湡涓€琛岋細`kernelsu <size> 4 - Live 0xffffffc0xxxxxxxx (O)`銆?
鐒跺悗鍦ㄦ墜鏈轰笂鎵撳紑 KernelSU Manager锛坢e.weishu.kernelsu锛夛紝涓婚〉搴旀樉绀?`Working <LKM> [Jailbreak mode]`銆佺増鏈?`32525-2` 宸﹀彸銆?鐢?Manager 缁?Termux 鎺堟潈鍚庯細`su` 鈫?`id` 搴旀樉绀?`uid=0(root) ... context=u:r:ksu:s0`銆?
## 4. 鍥炰紶鍐呭

- 绗?1 姝ョ殑鎻愭潈鏃ュ織灏鹃儴锛坧ipe-physrw-summary 閭ｅ嚑琛岋級+ boot_id
- 绗?2 姝ョ殑杈撳嚭锛堥潤榛?= 鎴愬姛锛夋垨宕╂簝杞偍鏂囦欢鍚?- 绗?3 姝ョ殑 `/proc/modules` 琛?+ Manager 鎴浘 + Termux su 杈撳嚭
- 鑻ヤ换浣曚竴姝ュけ璐ワ細瀹屾暣鏃ュ織 + 鎶ラ敊鍘熸枃

娉ㄦ剰浜嬮」锛氫竴鍒囬兘鏄槗澶辩殑锛堥噸鍚悗 root 涓?KSU 閮芥秷澶憋紝闇€閲嶈窇绗?1銆? 姝ワ級锛?涓嶈灏濊瘯鍒峰啓 boot 鍒嗗尯锛涘鏋?panic 浜嗙瓑鑷姩閲嶅惎鎭㈠锛孲ELinux 浼氳嚜鍔ㄥ洖鍒?Enforcing銆?