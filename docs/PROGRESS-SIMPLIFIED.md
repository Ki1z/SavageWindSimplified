# SavageWindSimplified 杩涘害涓庤璁℃枃妗?
> 鏈湴鍒嗘敮鐗规€э細NSP/XCI/NSZ 鍖呰В鍖呴泦鎴愩€?*绂佹涓婃父**锛堜笂娓?README 鎵胯 "never decrypts anything"锛夈€?> 鏈枃妗ｆ槸璁捐鍩虹嚎锛氬嚭闂鏃跺厛缈汇€屄? 鏁呴殰閫熸煡琛ㄣ€嶅垽鏂槸鍚﹀師鏈夎璁＄己闄凤紝鍐嶅姩浠ｇ爜銆?> 鏈€鍚庢洿鏂帮細2026-11-10

---

## 0. 椤圭洰瀹氫綅涓庣害鏉?
- 浠撳簱锛歚<ws>\SavageWindSimplified`锛堝厠闅嗚嚜 `<ws>\SavageWindRecomp`锛岃繙绋嬪悕 `upstream` 鎸囨湰鍦帮紝鍒嗘敮 `simplified`锛夈€?- 鐩爣锛氳缃〉鏂板"娓告垙鍖?+ prod.keys + title.keys"涓夋潯鐩?鈫?璋?nstool 瑙ｅ寘锛坋xefs/romfs/aoc1/aoc2锛夆啋 鐩存帴钀藉埌 SavageWind 鏈熸湜鐨勬渶缁堜綅缃?鈫?缂栬瘧銆?- 纭害鏉燂細
  1. 姘镐笉涓婃父锛涙案涓嶆彁浜?key/娓告垙鏂囦欢锛涙棩蹇椾笉鎵撳嵃浠讳綍瀵嗛挜鏉愭枡锛堢 `-v/--showkeys`锛夈€?  2. 澶嶇敤 SavageWind 鏃㈡湁鏈哄埗锛歚plat_spawn/log_path/kill/wait`锛坧latform.h:84锛夎窇澶栭儴杩涚▼銆佽繘搴︽潯 Esc/B 鍙栨秷锛坓amebuild 杩涚▼璺熻釜妯″紡锛夈€丼DL 鏂囦欢瀵硅瘽妗嗗甫杩囨护鍣ㄣ€乣savagewind.ini` 鎸佷箙鍖栥€?  3. 璺宠繃 `romfs_copy_tree`锛氭洿鏂?RomFS 鐩存帴瑙ｅ埌鏈€缁堜綅缃紙鐪佷竴娆?14GB 澶嶅埗锛夈€?  4. AGENTS.md 鎯緥缁х画閫傜敤锛堣繖鏄?SavageWind 鐨勪唬鐮佸簱锛夈€?
---

## 1. 鎬讳綋姝ラ璺嚎鍥?
| # | 闃舵 | 鐘舵€?| 鍐呭 |
|---|---|---|---|
| 0 | 椤圭洰楠ㄦ灦 | 鉁?| 鍏嬮殕浠撳簱銆佸缓 `simplified` 鍒嗘敮銆乺emote=upstream 鎸囨湰鍦?|
| 1 | nstool 鏋勫缓閫傞厤 | 鉁?| `<ws>\nstool`锛堝垎鏀?`simplified-build`锛夛紝浜х墿 `bin/nstool.exe`锛岃 搂3.1 |
| 2 | CLI 瀹炴祴楠岃瘉 | 鉁?| 涓夌鍖呮ā寮?+ XCI + NSZ 缁撴瀯 + key 璇婃柇锛岃 搂3.3 |
| 3 | `runtime/ncz.c` | 猬?| NCZ鈫扤CA 閲嶆瀯妯″潡锛坙ibzstd + AES-128-CTR锛夛紝瑙?搂3.5 绠楁硶 |
| 4 | NCZ 鍗曞厓娴嬭瘯 | 猬?| 鈶犲悎鎴?NCZ 寰€杩旓紙staged NCA鈫掑悎鎴愨啋閲嶆瀯鈫掑瓧鑺傛瘮瀵癸級锛涒憽榛勯噾 E2E锛歚test/games/nsz/*.nsz` 閲嶆瀯鍚庝笌 `test/work/m1/u/94a679....nca` 瀛楄妭姣斿 |
| 5 | 瑙ｅ寘椹卞姩 `runtime/nsp_extract.c` | 猬?| 闃舵鏈?A鈥揊 + 鍒ゅ瀷 + 棰勬 + 鍙栨秷锛岃 搂3.4/搂4 |
| 6 | UI 闆嗘垚 | 猬?| setup 椤典笁琛岋紙鍖呭閫?prod.keys/title.keys锛夈€佸梾鎺㈡牎楠屻€乮ni 鎸佷箙鍖?|
| 7 | i18n | 猬?| 13 璇█鏉＄洰 |
| 8 | vendor + 鎵撳寘 | 猬?| nstool 杩?third_party銆丮akefile 鐩爣銆乣make dist` 闅?zig/ 妯″紡鎵撳寘 |
| 9 | E2E 楠屾敹 | 猬?| 鐢?`test/games/` 涓夋ā寮?+ XCI + NSZ 鍏ㄦ祦绋嬭窇閫氾紱娓呯悊鏆傚瓨 |

渚濊禆鍏崇郴锛?鈫?鈫?鈫?鈫?/8鈫?銆? 涓?5 鍙苟琛岃捣姝ワ紙5 鐨?nstool 璋冪敤灞備笉渚濊禆 3锛夈€?
---

## 2. 鍏抽敭浜嬪疄閫熸煡锛堥粍閲戝熀鍑嗗€硷級

| 椤?| 鍊?| 鏉ユ簮 |
|---|---|---|
| BOTW 搴旂敤 ID | `01007EF00011E000` | launcher.mm:505 |
| 鏇存柊 TitleID / rights id | `01007EF00011E800`锛坱ik 鏂囦欢鍚嶅悗缂€ `...0003`锛?| A1 瀹炴祴 |
| DLC1 / DLC2 | `01007EF00011F001` / `01007EF00011F002`锛坮ights id 鍚庣紑鍏?0锛?| A3 瀹炴祴 |
| 1.9.0 build id锛坢ain 鍓?8 瀛楄妭锛?| `cd57b23fa4bbad65` | game_versions.h + A5 瀹炴祴涓€鑷?|
| 1.9.0 鍚堝苟 RomFS | **41062 鏂囦欢 / 15,129,626,830 瀛楄妭**锛沗System/Version.txt` = "1.9.0"锛? 瀛楄妭鏃犳崲琛岋級 | game_versions.h + A6 瀹炴祴鍒嗘涓嶅樊 |
| DLC1 RomFS | 7094 鏂囦欢 / 1326 MB锛沗System/AocVersion.txt` = "3.0" | A4 瀹炴祴锛圧EADME 绾?7000 鏂囦欢/1.4GB锛?|
| DLC2 RomFS | 1 鏂囦欢 `System/AocVersion.txt` | A4 瀹炴祴 |
| 鏈綋 program NCA 鍐呭 ID | `861707001401bbce36c7e421efac76d4`锛圢SP 涓?XCI 鐩稿悓 鈫?绛変环锛?| A2/B3 瀹炴祴 |
| 鏇存柊 program NCA 鍐呭 ID | `94a679f15eb8b1a5ee59b8c284f71ae3`锛圢SP/NSZ 鐩稿悓锛?| A1/NSZ 瑙ｆ瀽 |
| DLC NCA 鍐呭 ID | F001=`b201291d3e099872debd1c1870572cc3`锛孎002=`a6ee60de4f804ac979f18fb68ced5857` | A4 瀹炴祴 |
| nstool 鐗堟湰 | v0.0.0锛坉evelopment-tip 绯伙級锛岄潤鎬?10.7MB | 鍐掔儫娴嬭瘯 |

榛勯噾鏆傚瓨锛堥獙鏀跺悗娓呯悊锛夛細`test/work/m1/{b,u,exefs,romfs,d1,d2,xci}`銆乣test/work/{m2,m3}`銆佹棩蹇?`test/work/*.log`銆傚悎璁?~75GB銆?
---

## 3. 宸插畬鎴愭敼杩涗笌瀹炵幇缁嗚妭

### 3.1 nstool 鏋勫缓閫傞厤锛坣stool 浠撳簱锛屽垎鏀?`simplified-build`锛?
| 鎻愪氦 | 鍐呭 | 缁嗚妭/鏍瑰洜 |
|---|---|---|
| `2236bb9` | `src/CnmtProcess.cpp:218` | 涓婃父 bug锛歚fmt::print("{:s}{:d}\n", i)` 涓や釜鍗犱綅绗︿竴涓弬鏁帮紝fmt v10 consteval 鎷掔紪 鈫?琛?`prefix, i` |
| `0898c00` | makefile + `build_ucrt64.sh` | 鈶燙XXFLAGS `c++11`鈫抈c++20`锛坈++11 澶棫锛夛紱鈶IN32 閾炬帴鍔?`LINKFLAGS=-municode`锛歯stool 鍏ュ彛鏄?`UnicodeMain.h` 閲岀殑 **`wmain`**锛宮ingw 榛樿 ANSI 鍚姩妗╂壘涓嶅埌 `main` 鎵嶉€€鍥炴壘 `WinMain`鈥斺€斾笉鍔?`-municode` 蹇呮姤 `undefined reference to WinMain`锛涒憿涓€閿剼鏈?|
| `4b03518` | `.gitattributes` | `*.sh text eol=lf` + 浠撳簱 `core.autocrlf=false`锛岄槻 CRLF 鐮村潖 bash 鑴氭湰 |
| `3590b3d` | `Settings.cpp` | 瑙ｆ敞閲?`--titlekeyset`锛堜笂娓告敞閲婃帀浜嗭紝Settings.cpp:642锛夊苟琛ョ敤娉曟枃鏈€?*nsz/NSP 鎻愬彇鐨?keyset 涓昏矾寰?* |

瀛愭ā鍧?`deps/libtoolchain`锛堝垎鏀?`simplified-build` @ `adeb4cd`锛夛細`src/cli/FormatUtil.cpp:84` 涓夊厓琛ㄨ揪寮忚繍琛屾椂鏍煎紡涓?鈫?if/else 鎷嗗垎锛坒mt v10 consteval锛夈€傜埗浠撳簱璇ユ寚閽堟樉绀?`M` 灞為鏈熴€?
**鏋勫缓閰嶆柟**锛歚<ws>\nstool\build_ucrt64.sh`锛圡SYS2 UCRT64 gcc 15.2.0锛沗bash build_ucrt64.sh` 鍏ㄩ噺 clean 鏋勫缓锛屽凡楠岃瘉锛夈€傛敞鎰忥細deps 椤跺眰 foreach 鐢?`;` 鍒嗛殧锛?*瀛愭瀯寤哄け璐ヤ笉浼氫腑鏂摼**鈥斺€旀鏌?`deps/*/bin/*.a` 浜斾釜閮藉湪鎵嶇畻杩囥€?
**pwsh鈫攂ash 澶囧繕**锛歱wsh 5.1 浼犲惈鍙屽紩鍙风殑瀛楃涓茬粰 `bash -lc '...'` 浼氳鎷嗗潖锛堝巻鍙插娆¤寮傚け璐ョ殑鏍规簮锛夆啋 bash 鍛戒护涓€寰嬪啓杩?.sh 鑴氭湰鏂囦欢鎵ц锛涜皟鍘熺敓 exe 闇€ `export MSYS2_ARG_CONV_EXCL='*'`锛堝惁鍒?`-x /0/` 琚敼鍐欐垚 `<msys2根（本机 D:\msys2）>\0`锛汼avageWind 鐢?C 鐩存帴 CreateProcess 鏃犳闂锛夈€?
### 3.2 CLI 瀹炴祴鐭╅樀锛堝叏閮ㄩ€氳繃锛?
| 娴嬭瘯 | 鍛戒护瑕佺偣 | 缁撴灉 |
|---|---|---|
| 鏇存柊鍖呮祬鎻愬彇+鍒ゅ瀷 | `nstool -k prod.keys --titlekeyset t.keys -x <stage>/ <nsp>` 鍚庨€?NCA info | 琛ヤ竵 NCA锛歚Program` + ProgID **E000** + 鍒嗗尯 AesCtr/AesCtrEx |
| 鏈綋鍖呭悓涓?| 鍚屼笂 | 鏈綋 NCA锛歚Program` + ProgID E000锛岀函 AesCtr锛?3.8GB |
| DLC脳2 | 鍚屼笂 | `PublicData` + ProgID F001/F002锛汻omFS 鍦?*鍒嗗尯 0**锛堝崟鍒嗗尯锛?|
| exefs 鎻愬彇 | `-x /0/ out/ <patch.nca>` | main(17.9MB)+npdm+rtld+sdk+subsdk0锛沗nstool main` 鈫?`ModuleId: cd57b23fa4bbad65...` 鉁?|
| **BKTR 鍚堝苟** | `--basenca <base.nca> -x /1/ out/ <patch.nca>` | **41062 鏂囦欢/15,129,626,830 瀛楄妭**锛孷ersion.txt="1.9.0" 鉁?|
| XCI | `-x <stage>/ <xci>` | secure 涓?4 NCA锛屾湰浣撳唴瀹?ID 涓?NSP 鐩稿悓 鈫?绛変环 |
| NSZ 缁撴瀯 | 鐩存帴瑙ｆ瀽瀹瑰櫒 | 瑙?搂3.5 |

### 3.3 鍒ゅ瀷涓庢彁鍙栧畾绋胯鍒欙紙瀹炵幇鐓ф妱锛?
```
娴呮彁鍙栵細nstool -k prod.keys --titlekeyset title.keys -x <staging>/ <姣忎釜鍖?
  鈫?NCA锛堝唴瀹?ID 鍛藉悕锛? .tik/.cert锛圢SP锛? secure 瀛愮洰褰曪紙XCI锛?鍒ゅ瀷锛坕nfo 杩愯锛屽彧闇€ nca_header_key锛涙棤 titlekey 浠?WARNING 涓嶅け璐ワ級锛?  Program + 鍒嗗尯1=AesCtrEx      鈫?鏇存柊锛堣ˉ涓侊級program NCA
  Program + 鍏?AesCtr           鈫?鏈綋 program NCA锛堝悓 ProgID 涓庤ˉ涓侀厤瀵光€斺€斿疄娴?ProgID=搴旂敤ID锛?  PublicData + ProgID 鍓嶇紑鍖归厤  鈫?DLC锛堟寜 ProgID 鎺掑簭 鈫?aoc1/aoc2锛?  Meta / Control / Manual       鈫?璺宠繃锛圡eta 鐨?ProgID 鏄ˉ涓?TitleID E800锛屽嬁娣风敤锛?鎻愬彇锛堥渶 prod.keys + title.keys锛夛細
  琛ヤ竵 NCA `-x /0/` 鈫?exefs/    锛圓esCtr锛屾棤闇€ base锛夆啋 绔嬪嵆楠?build id锛坒ail fast锛屽厛浜?14GB RomFS锛?  琛ヤ竵 NCA `-x /1/ --basenca <鏈綋>` 鈫?romfs/锛?瀹屾暣 1.9.0 鍚堝苟鏍戯級
  DLC NCA  `-x /0/` 鈫?aoc1/ aoc2/锛堟敞鎰忓垎鍖虹储寮?0锛乸rogram 鎵嶆槸 1锛?```

閰嶅瑙勫垯缁嗚妭锛氭暣鍚堝寘涓湰浣?琛ヤ竵 Program NCA 骞跺瓨锛堝悓 ProgID E000锛夛紝**闈?AesCtrEx 鍖哄垎**锛沶stool 瀵规棤 base 鐨勮ˉ涓佷細鎵?`[WARNING] NCA Partition 1 not readable. (Base NCA not supplied...)` 鈥斺€?杩欐潯璀﹀憡鏈韩鍙綔涓?杩欐槸琛ヤ竵"鐨勬梺璇併€?
### 3.4 NSZ 鍒嗗潡 solid 鍙樹綋锛堝疄娴嬭В鐮?+ ncz.c 绠楁硶锛?
`test/games/nsz/*.nsz` 瀹炴祴缁撴瀯锛堝唴瀹?ID `94a679f1...` 涓庡凡楠岃瘉琛ヤ竵 NCA 鐩稿悓锛夛細

```
[0x0000]  NCA 澶村師鏍?(0x4000)
[0x4000]  b"NCZSECTN" + u64 count        鈫?鏈枃浠?count=534锛堝垎鍧楀彉浣擄紱缁忓吀鍙樹綋 count鈮?锛?[0x4010]  count 脳 64B 鏉＄洰锛?          offset u64 | size u64 | cryptoType u64 | pad u64 | cryptoKey[16] | cryptoCounter[16]
          锛圢SCB compressor.py:300-319 鏉冨▉甯冨眬锛涘疄娴嬫潯鐩亸绉婚摼杩炵画瑕嗙洊鏁翠釜 NCA 浣擄級
[0x10+count*64]  鍗曟潯 zstd solid 甯э紙28 B5 2F FD锛夊埌 EOF
```

ncz.c 绠楁硶锛堢収 NSCB `decompressor.py` 閫氱敤瀹炵幇锛屽ぉ鐒跺吋瀹逛袱绉嶅彉浣擄級锛?1. 璇?0x4000 澶?鈫?鍘熸牱鍐欒緭鍑猴紱
2. 璇?magic+count+鏉＄洰琛紱
3. zstd 娴佸紡瑙ｅ帇鍓╀綑閮ㄥ垎杩藉姞鍒拌緭鍑猴紙娉ㄦ剰 libzstd `ZSTD_decompressStream` 甯х粨鏉熻繑鍥?0 鍚庤閲嶇疆鍐嶇画锛屽吋瀹瑰甯э級锛?4. 閫愭潯鐩師鍦伴噸鍔犲瘑锛歚cryptoType==1` 璺宠繃锛堟槑鏂囷級锛宍!=3` 鎶ラ敊锛堝彧鏀寔 CTR锛夛紝鍚﹀垯 AES-128-CTR 浠?`offset` 鍒?`offset+size`锛宑ounter 鏈夋晥鍊?= **counter 澶х u128 + (鏂囦欢鍋忕Щ >> 4)**锛坉ecompressor.py:35 `initial_value=(offset>>4)`锛夈€?5. 楠岃瘉鎵嬫锛氳緭鍑轰笌 `test/work/m1/u/94a679....nca` 瀛楄妭姣斿銆?
鍙傝€冭鍙細NSC_BUILDER 涓?MIT锛坧y/ztools/lib/{decompressor,compressor}.py銆丗s/Nsp.py:637+锛夛紱**绂佺敤 GPL 鐨?nsz 宸ュ叿浠ｇ爜**銆傚潡琛ㄥ彉浣擄紙NCZBLOCK锛変笉鏀寔 鈫?鏄庣‘鎶ラ敊銆?
### 3.5 Key 璇婃柇瑙勫垯锛堝疄娴?9 缁?keyset 瀹氭锛?
**鏍瑰洜锛歵itle.keys 鐨?rights id 瑕嗙洊闈?*锛堝悇涓绘満鏈湴搴撲笉鍚岋級锛屼笌 master_key 浠ｆ鏃犲叧锛?
| 鐗堟湰 | 鏉＄洰鏁?| e000 鏈綋 | e800 鏇存柊 | f001/f002 | 瀹炴祴 |
|---|---|---|---|---|---|
| 16.1.0 / 17.0.0 | 288 | 鉁?| 鉁?| 鉁?| 鏈綋鎶ラ敊锛?鐢ㄦ埛鎵€瑙侊級锛屾洿鏂?DLC 濂?|
| 18.0.0 | 986 | 鉁?| 鉁?| 鉁?| 鍏ㄥソ |
| 18.1.0 / 19.0.0 | 18 | 鉁?| 鉁?| 鉁?| DLC 鎶ラ敊 |
| 20.2.0 | 6 | 鉁?| 鉁?| 鉁?| 鍏ㄦ姤閿?|
| 21.1.0 | 169 | 鉁?| 鉁?| 鉁?| 鏈綋鎶ラ敊 |
| 22.5.0 | 19 | 鉁?| 鉁?| 鉁?| 鍏ㄦ姤閿?|
| example | 179 | 鉁?| 鉁?| 鉁?| 鍏ㄥソ |

- 澶辫触褰㈡€侊細`[WARNING] NCA Partition 0 not readable. (AES-CTR Key was not determined)`锛圓7 鐭╅樀瀹炴祴澶嶇幇锛夈€?- prod.keys锛欱OTW 鍏ㄨ蛋 titlekey 璺緞 鈫?master_key 鏁伴噺锛?7鈫?3锛夋棤鍏筹紱`nca_header_key` 缂哄け鍙敱 `nca_header_kek_source`+`nca_header_key_source`+`master_key_00` 娲剧敓锛坣stool 鑷姩锛夈€?- **椹卞姩璁捐淇**锛氬垽鍨嬶紙鍙渶 header key锛夆啋 璇诲嚭 rights id 鈫?**鍏堟牳瀵?title.keys 瑕嗙洊**鍐嶅惎鍔ㄩ暱鎻愬彇锛涚己澶卞垯鎶?title.keys 缂哄皯 XXX 鐨勮В瀵嗘潯鐩?骞跺垪鍑虹己鐨?id銆傚鐢ㄨ矾寰勶細nstool `--tik <绁ㄦ枃浠?`锛圢SP 鑷甫 tik/cert锛岄殢娴呮彁鍙栬惤鐩橈級銆?
---

## 4. 瑙ｅ寘椹卞姩璁捐瑕佺偣锛堝疄鐜板墠鐨勫畾绋匡級

- 闃舵鏈猴細A 娴呮彁鍙栧叏閮ㄥ寘 鈫?B 鍒ゅ瀷鍒嗙被 鈫?C 瀵嗛挜棰勬 鈫?D exefs 鎻愬彇 + build id fail-fast锛?.9.0 涓嶅湪琛?鈫?鏄庣‘鎶ョ増鏈級鈫?E romfs/aoc 鎻愬彇锛堢洿鎺ヨ惤鏈€缁堜綅缃級鈫?F 娓呯悊 staging銆?- 鍙栨秷锛歱lat_spawn 璧风殑 nstool 杩涚▼绾冲叆 gamebuild 鍚屾璺熻釜锛孍sc/B 鈫?kill 杩涚▼鏍?鈫?鍒?staging銆?- 杩涘害锛歯stool 鏃犺繘搴﹁緭鍑猴紙`Saving ...` 琛屾湁鏂囦欢鍚嶏級鈫?鎸夊凡鎻愬彇瀛楄妭/棰勪及鎬婚噺杩戜技锛涢樁娈甸棿鍒锋柊鏂囨銆?- 澶辫触鍗冲仠锛坒ail fast锛? 宸插垹 staging锛涢敊璇爜杩?i18n銆?- Windows 闀胯矾寰勶細staging 鏀?data 鏍逛笅鐭悕鐩綍锛堝 `<data>/sw_cache/`锛夛紝閬垮厤璺緞瓒呴暱銆?- GAME_FILES[] 鍏佽鍚嶅崟鏂板瑙ｅ寘浜х墿鏉＄洰 + `.gitignore`銆?
---

## 5. 宸茬煡绾︽潫涓庨闄╂竻鍗?
1. 涓婃父绛栫暐锛堟案涓嶄笂娓搞€佹棩蹇楁棤瀵嗛挜锛夆€斺€旇 搂0銆?2. nstool 琛屼负锛氳鍛婇潪鑷村懡锛沬nfo 闃舵琛ヤ竵 NCA 蹇呮湁涓€鏉?base 璀﹀憡锛堝嬁褰撻敊璇級锛汼parse Storage 涓嶆敮鎸侊紙BOTW 涓嶅彈褰卞搷锛夛紱`--titlekeyset` 涓嶄紶鍒欏洖钀?`$HOME/.switch/`锛圵indows 涓?= `%USERPROFILE%\.switch`锛屽嬁渚濊禆锛夈€?3. nstool 鏃犺繘搴﹁緭鍑猴紱鏃?`--quiet`锛涢敊璇€氳繃闈為浂閫€鍑虹爜浼犻€掋€?4. NCZ锛氫粎 solid锛堝惈鍒嗗潡 solid锛夛紱鍧楄〃鍙樹綋鏄庣‘鎷掔粷锛汫PL nsz 宸ュ叿浠ｇ爜绂佺敤銆?5. fmt v10 consteval锛歯stool 鍏ㄩ噺閲嶅缓鏃朵笁澶勮ˉ涓佺己涓€涓嶅彲锛埪?.1锛夈€?6. 鏁村悎鍖呭弻 Program NCA 闈?AesCtrEx 鍖哄垎锛汳eta ProgID 鏄ˉ涓?TitleID锛屽嬁鐢ㄤ綔閰嶅閿€?7. 娴嬭瘯鑴氭湰涓撴湁锛歁SYS2 鍙傛暟杞崲銆乸wsh 寮曞彿銆丆RLF锛埪?.1 澶囧繕锛夆€斺€斾笉褰卞搷 SavageWind 鏈綋锛圕reateProcess锛夈€?
---

## 6. 鏁呴殰閫熸煡琛紙鐥囩姸 鈫?鏍瑰洜 鈫?绔犺妭锛?
| 鐥囩姸 | 鏈€鍙兘鏍瑰洜 | 绔犺妭 |
|---|---|---|
| `Partition X not readable. (AES-CTR Key was not determined)` | title.keys 缂鸿 rights id 鏉＄洰锛坘eyset 瑕嗙洊闈級 | 搂3.5 |
| `undefined reference to WinMain` | 閲嶇紪 nstool 灏戜簡 `-municode` | 搂3.1 |
| fmt consteval 缂栬瘧閿欒 | 涓夊 fmt v10 琛ヤ竵缂轰竴 | 搂3.1 |
| NCZ 閲嶆瀯鍚庡搱甯屼笉绗?| 鏉＄洰甯冨眬璇婚敊锛?4B锛侊級鎴?counter 鏁板閿欙紙澶х u128 + off>>4锛?| 搂3.4 |
| zstd 娴佷腑閫斿け璐?| 甯х粨鏉熷悗鏈噸缃?ZSTD_decompressStream锛堝甯у吋瀹癸級 | 搂3.4 |
| 琛ヤ竵 RomFS 鎻愬彇鎶?base 缂哄け | 蹇樹簡 `--basenca` | 搂3.3 |
| DLC 鎻愬彇涓虹┖/鍙湁 AocVersion | 鐢ㄩ敊鍒嗗尯绱㈠紩锛圖LC 鏄?/0/ 涓嶆槸 /1/锛?| 搂3.2/3.3 |
| 鏈綋/琛ヤ竵鍒嗕笉娓?| 娌＄敤 AesCtrEx 鍒ゅ埆锛岃鐢?Meta 鐨?ProgID | 搂3.3 |
| MSYS2 娴嬭瘯鑴氭湰鍙傛暟鎬紓 | ARG_CONV_EXCL / pwsh 寮曞彿 / CRLF | 搂3.1 |
| 鍚堝苟 RomFS 鏁扮洰瀵逛笉涓?41062 | base/patch 鍐呭 ID 涓嶉厤瀵癸紙涓嶅悓鐗堟湰娣峰寘锛?| 搂2 |

---

## 7. 鍙樻洿璁板綍

| 鏃ユ湡 | 浜嬮」 |
|---|---|
| 2026-11-10 鍓?| 椤圭洰鍏嬮殕锛沶stool 浜斾緷璧栨瀯寤猴紱fmt脳2 淇锛?236bb9 / adeb4cd锛?|
| 2026-11-10 | makefile 閫傞厤+鏋勫缓鑴氭湰锛?898c00锛夛紱gitattributes锛?b03518锛夛紱`--titlekeyset` 鍚敤锛?590b3d锛夛紱`bin/nstool.exe` 浜у嚭 |
| 2026-11-10 | CLI 瀹炴祴鏀跺畼锛歂SP 涓夋ā寮忋€乣--basenca` 鍚堝苟瀵硅处銆丏LC銆乆CI 绛変环銆丯SZ 鍒嗗潡鍙樹綋瑙ｇ爜銆? 缁?key 璇婃柇锛圓1鈥揂7/B1鈥揃3锛?|
| 2026-11-10 | 鏈枃妗ｅ缓绔?|
