# SavageWindSimplified 进度与设计文档

> 本地分支特性：NSP/XCI/NSZ 包解包集成。**禁止上游**（上游 README 承诺 "never decrypts anything"）。
> 本文档是设计基线：出问题时先翻「§6 故障速查表」判断是否原有设计缺陷，再动代码。
> 最后更新：2026-11-10

---

## 0. 项目定位与约束

- 仓库：`<ws>\SavageWindSimplified`（克隆自 `<ws>\SavageWindRecomp`，远程名 `upstream` 指本地，分支 `simplified`）。
- 目标：设置页新增"游戏包 + prod.keys + title.keys"三条目 → 调 nstool 解包（exefs/romfs/aoc1/aoc2）→ 直接落到 SavageWind 期望的最终位置 → 编译。
- 硬约束：
  1. 永不上游；永不提交 key/游戏文件；日志不打印任何密钥材料（禁 `-v/--showkeys`）。
  2. 复用 SavageWind 既有机制：`plat_spawn/log_path/kill/wait`（platform.h:84）跑外部进程、进度条 Esc/B 取消（gamebuild 进程跟踪模式）、SDL 文件对话框带过滤器、`savagewind.ini` 持久化。
  3. 跳过 `romfs_copy_tree`：更新 RomFS 直接解到最终位置（省一次 14GB 复制）。
  4. AGENTS.md 惯例继续适用（这是 SavageWind 的代码库）。

---

## 1. 总体步骤路线图

| # | 阶段 | 状态 | 内容 |
|---|---|---|---|
| 0 | 项目骨架 | ✅ | 克隆仓库、建 `simplified` 分支、remote=upstream 指本地 |
| 1 | nstool 构建适配 | ✅ | `<ws>\nstool`（分支 `simplified-build`），产物 `bin/nstool.exe`，见 §3.1 |
| 2 | CLI 实测验证 | ✅ | 三种包模式 + XCI + NSZ 结构 + key 诊断，见 §3.2/§3.5 |
| 3 | `runtime/ncz.c` | ✅ | NCZ→NCA 重建模块（libzstd 1.5.7 + 计算式 AES-128-CTR），见 §3.4 |
| 4 | NCZ 单元测试 | ✅ | 17 项合成检查 + 真实 NSZ 黄金验收（sha256 = 内容 ID），见 §3.4 |
| 5 | 解包驱动 `runtime/nsp_extract.c` | ✅ | 阶段 A–F + 判型 + 预检 + 取消；合成测试 51 项全绿（stub nstool 全流程），见 §3.6/§4 |
| 6 | UI 集成 | ✅ | setup 页"从游戏包"分区（包多选/prod.keys/title.keys + Set up from packages）、ini 持久化、Esc/B 取消、NSPE_* 失败文案映射；clang 语法检查 + 51/17 回归全绿，见 §3.7 |
| 7 | i18n | ✅ | 38 新键（36 译 + prod.keys/title.keys 保留英文名）+ 8 驱动步骤名，13 语言全补；旧迎宾键移除；`python tools/i18n_check.py` 13 语言 0 缺失（exit 0） |
| 8 | vendor + 打包 | ✅ | 不入库二进制，走兄弟 checkout 构建模式（同 zig/FSR 的 fetch 模式）：`make build/nstool/nstool.exe` 增量构建 + `make dist` 打包 `nstool/nstool.exe` + MIT LICENSE + README 组件行；包内 `--titlekeyset` 已验证可用；见 §3.8 |
| 9 | E2E 验收 | ✅ | 真实 dump：本体+更新整合包 + DLC1 + DLC2(空壳) ⇒ 提取 48,162 文件/15.41GB + aoc1 7,094/1.29GB；生成器+编译（clang）→ swgame.dll 227MB → 启动进世界 60fps（125,144+13,936+40,516 函数，160 sites）。XCI/NSZ 由其他测试者反馈。见 §3.10 |

依赖关系：0→1→2→3→4→5（6/7/8 可并行）；5 的 nstool 调用层不依赖 3。

---

## 2. 关键事实速查（黄金基准值）

| 项 | 值 | 来源 |
|---|---|---|
| BOTW 应用 ID | `01007EF00011E000` | launcher.mm:505 |
| 更新 TitleID / rights id | `01007EF00011E800`（tik 文件名后缀 `...0003`） | A1 实测 |
| DLC1 / DLC2 | `01007EF00011F001` / `01007EF00011F002`（rights id 后缀全 0） | A3 实测 |
| 1.9.0 build id（main 前 8 字节） | `cd57b23fa4bbad65` | game_versions.h + A5 实测一致 |
| 1.9.0 合并 RomFS | **41062 文件 / 15,129,626,830 字节**；`System/Version.txt` = "1.9.0"（5 字节无换行） | game_versions.h + A6 实测分毫不差 |
| DLC1 RomFS | 7094 文件 / 1326 MB；`System/AocVersion.txt` = "3.0" | A4 实测（README 约 7000 文件/1.4GB） |
| DLC2 RomFS | 1 文件 `System/AocVersion.txt` | A4 实测 |
| 本体 program NCA 内容 ID | `861707001401bbce36c7e421efac76d4`（NSP 与 XCI 相同 → 等价） | A2/B3 实测 |
| 更新 program NCA 内容 ID | `94a679f15eb8b1a5ee59b8c284f71ae3`（NSP/NSZ 相同） | A1/NSZ 解析 |
| DLC NCA 内容 ID | F001=`b201291d3e099872debd1c1870572cc3`，F002=`a6ee60de4f804ac979f18fb68ced5857` | A4 实测 |
| 更新 NCA 大小 | 0x3AE96C00 = 988,376,064 字节（sha256 = 内容 ID） | NSZ 解码实测 |
| nstool 版本 | v0.0.0（development-tip 系），静态 10.7MB | 冒烟测试 |

黄金暂存（验收后清理）：`test/work/m1/{b,u,exefs,romfs,d1,d2,xci}`、`test/work/{m2,m3}`、日志 `test/work/*.log`。合计 ~75GB。

---

## 3. 已完成改进与实现细节

### 3.1 nstool 构建适配（nstool 仓库，分支 `simplified-build`）

| 提交 | 内容 | 细节/根因 |
|---|---|---|
| `2236bb9` | `src/CnmtProcess.cpp:218` | 上游 bug：`fmt::print("{:s}{:d}\n", i)` 两个占位符一个参数，fmt v10 consteval 拒编 → 补 `prefix, i` |
| `0898c00` | makefile + `build_ucrt64.sh` | ①CXXFLAGS `c++11`→`c++20`；②WIN32 链接加 `LINKFLAGS=-municode`：nstool 入口是 `UnicodeMain.h` 里的 **`wmain`**，mingw 默认 ANSI 启动桩找不到 `main` 才退回找 `WinMain`——不加 `-municode` 必报 `undefined reference to WinMain`；③一键脚本 |
| `4b03518` | `.gitattributes` | `*.sh text eol=lf` + 仓库 `core.autocrlf=false`，防 CRLF 破坏 bash 脚本 |
| `3590b3d` | `Settings.cpp` | 解注掉 `--titlekeyset`（上游注释掉了，Settings.cpp:642）并补用法文本。**nsz/NSP 提取的 keyset 主路径** |

子模块 `deps/libtoolchain`（分支 `simplified-build` @ `adeb4cd`）：`src/cli/FormatUtil.cpp:84` 三元表达式运行时格式串 → if/else 拆分（fmt v10 consteval）。父仓库该指针显示 `M` 属预期。

**构建配方**：`<ws>\nstool\build_ucrt64.sh`（MSYS2 UCRT64 gcc 15.2.0；`bash build_ucrt64.sh` 全量 clean 构建，已多次验证）。注意：deps 顶层 foreach 用 `;` 分隔，**子构建失败不会中断链**——检查 `deps/*/bin/*.a` 五个都在才算过。

**pwsh→bash 备忘**：pwsh 5.1 传含双引号的字符串给 `bash -lc '...'` 会被拆坏（历史多次诡异失败的根源）→ bash 命令一律写进 `.sh` 脚本文件执行；调原生 exe 需 `export MSYS2_ARG_CONV_EXCL='*'`（否则 `-x /0/` 被改写成本机 MSYS2 安装路径下的 `\0`；SavageWind 用 C 直接 CreateProcess 无此问题）。

### 3.2 CLI 实测矩阵（全部通过）

| 测试 | 命令要点 | 结果 |
|---|---|---|
| 更新包浅提取+判型 | `nstool -k prod.keys --titlekeyset t.keys -x <stage>/ <nsp>` 后逐个 NCA info | 补丁 NCA：`Program` + ProgID **E000** + 分区 AesCtr/AesCtrEx |
| 本体包同上 | 同上 | 本体 NCA：`Program` + ProgID E000，纯 AesCtr，2.3GB |
| DLC×2 | 同上 | `PublicData` + ProgID F001/F002；RomFS 在**分区 0**（单分区） |
| exefs 提取 | `-x /0/ out/ <patch.nca>` | main(17.9MB)+npdm+rtld+sdk+subsdk0；`nstool main` → `ModuleId: cd57b23fa4bbad65...` ✅ |
| **BKTR 合并** | `--basenca <base.nca> -x /1/ out/ <patch.nca>` | **41062 文件/15,129,626,830 字节**，Version.txt="1.9.0" ✅ |
| XCI | `-x <stage>/ <xci>` | secure 下 4 NCA，本体内容 ID 与 NSP 相同 → 等价 |
| NSZ 结构 | 直接解析容器 | 见 §3.4 |

### 3.3 判型与提取定稿规则（实现照抄）

```
浅提取：nstool -k prod.keys --titlekeyset title.keys -x <staging>/ <每个包>
  → NCA（内容 ID 命名）+ .tik/.cert（NSP）+ secure 子目录（XCI）
判型（info 运行，只需 nca_header_key；无 titlekey 仅 WARNING 不失败）：
  Program + 分区1=AesCtrEx       → 更新（补丁）program NCA
  Program + 全 AesCtr            → 本体 program NCA（同 ProgID 与补丁配对）
  PublicData + ProgID 前缀匹配   → DLC（按 ProgID 排序 → aoc1/aoc2）
  Meta / Control / Manual        → 跳过（Meta 的 ProgID 是补丁 TitleID E800，易混用）
提取（需 prod.keys + title.keys）：
  补丁 NCA `-x /0/` → exefs/     （AesCtr，无需 base）→ 立即验 build id（fail fast，先于 14GB RomFS）
  补丁 NCA `-x /1/ --basenca <本体>` → romfs/（完整 1.9.0 合并树）
  DLC NCA  `-x /0/` → aoc1/ aoc2/（注意分区索引 0；program 才是 1）
```

配对规则细节：整合包中本体/补丁 Program NCA 并存（同 ProgID E000），**靠 AesCtrEx 区分**；nstool 对无 base 的补丁会打 `[WARNING] NCA Partition 1 not readable. (Base NCA not supplied...)` —— 这条警告本身可作"这是补丁"的旁证。分区 ≠ 顺序：更新 NCA 的分区表 [0]=exefs(28MB，文件尾部) 、[1]=romfs(960MB，从 0x4000 开始)。`/0/`、`/1/` 指 **fs 头索引**，不是分区表顺序。

### 3.4 NCZ 格式与 ncz.c（实测解码 + 最终语义）

`test/games/nsz/*.nsz` 实测结构（内容 ID `94a679f1...` 与已验证补丁 NCA 相同）：

```
[0x0000]  NCA 头原样 (0x4000)
[0x4000]  b"NCZSECTN" + u64 count         → 本文件 count=534（分块变体；经典变体 count≤4）
[0x4010]  count × 64B 条目：
          offset u64 | size u64 | cryptoType u64 | pad u64 | cryptoKey[16] | cryptoCounter[16]
          （权威布局：nsz-rs `format/ncz.rs` 与 NSCB compressor.py:300-319；实测条目偏移连续覆盖整个 NCA 体）
[0x10+count*64]  单条 zstd solid 帧（28 B5 2F FD）到 EOF；块表变体则此处是 "NCZBLOCK"
```

实测统计：534 条目的 size 之和 = 0x3AE92C00，加 0x4000 = 0x3AE96C00（= NCA 大小，无缺口）；全部 cryptoType=3，**共用同一 key**；counter 仅 15 种，形如 `BE32(gen)||BE32(sec)||0^8`（如 `00000002 00000009`）。

**最终语义（已验证，勿再改）**：
1. 读 0x4000 头 → 原样写输出；
2. 读 magic+count+条目表；随后 8 字节若是 `NCZBLOCK` **明确报错**（块表变体不支持）；
3. zstd 流式解压剩余部分追加到输出（`ZSTD_decompressStream`；帧结束返回 0 后必须能继续处理后续帧）；
4. 逐条目原地重加密：`cryptoType==1` 跳过；`3`/`4` 走 AES-128-CTR；其它报错。CTR 计数器块 = **条目 counter 前 8 字节作 prefix + 绝对文件块号（offset>>4）的大端 u64**。经典 NSCB 条目把 counter 存成 `nonce||0`（绝对块号从该段起点算出），分块变体存 gen/sec 对，同一公式两者通吃；
5. 验收判据（nsz 工具自带）：**`sha256(重建 NCA)[0:32]` 必须等于 `.ncz` 文件名**（NCA 内容 ID = 整文件 sha256）。
   实测：重建 988MB NCA 的 sha256 = `94a679f15eb8b1a5ee59b8c284f71ae3b6296755af60946cb145989615d10344`，与原始 NCA **字节完全一致** ✅

**实现要点（runtime/ncz.c）**：
- `third_party/zstd/zstddeclib.c`（zstd 1.5.7 解码合并源，BSD；`README.md` 记重新生成命令）。
- AES-128 加密方向手写：S 盒由 GF(2^8) 逆元 + 仿射变换现算（**生成元必须用 3**），T 表加速；`bool (*NczProgress)(void*, uint64_t)` 回调支持取消；全部 64 位 seek（Windows `long` 32 位，必须 `_fseeki64`）。
- 输出文件必须 `fopen(..., "wb+")`：原地重加密要读回自己写的段。

**本轮踩过的坑（都已在代码/测试里固化）**：
1. **S 盒逆元索引越界**：`gexp[255 - glog[i]]` 在 `glog[1]==0` 时索引 255（数组 0..254），读到垃圾 → `sbox[1]` 错成 0x63（应 0x7c）。FIPS-197 单向量恰好不含字节 0x01 而漏检 → 现已加第二个 NIST SP 800-38A 向量 + 黄金哈希双保险。
2. **流式 zstd 截断保护误判**：帧正常完成后，再用空输入调一次 `ZSTD_decompressStream` 会返回"还需输入"(>0)，曾被当成截断。现用 `frame_done` 状态区分"帧完成"与"真截断"。
3. **计数器语义反复**：曾误改成"段内相对块号"，导致 534 段全部错位；以 nsz 的 `crypto.seek(i)`（绝对偏移）为准。
4. 合成夹具设计：截断测试必须在表内真截断（不能靠随后的帧字节凑够条目）；counter 低 64 位按真实文件写成 0。

参考许可：NSC_BUILDER `py/ztools/lib/{decompressor,compressor}.py`、Fs/Nsp.py 为 MIT；nsz 工具为 GPL（**仅读其格式定义/语义，不抄代码**）；nsz-rs（MIT）的 `format/ncz.rs` 是布局旁证。

### 3.5 Key 诊断规则（实测 9 组 keyset 定案）

**根因：title.keys 的 rights id 覆盖面**（各主机本地库不同），与 master_key 代次无关。

| 版本 | 条目数 | e000 本体 | e800 更新 | f001/f002 | 实测 |
|---|---|---|---|---|---|
| 16.1.0 / 17.0.0 | 288 | ✅ | ✅ | ✅ | 本体报错（≠用户所见），更新 DLC 好 |
| 18.0.0 | 986 | ✅ | ✅ | ✅ | 全好 |
| 18.1.0 / 19.0.0 | 18 | ✅ | ✅ | ✅ | DLC 报错 |
| 20.2.0 | 6 | ✅ | ✅ | ✅ | 全报错 |
| 21.1.0 | 169 | ✅ | ✅ | ✅ | 本体报错 |
| 22.5.0 | 19 | ✅ | ✅ | ✅ | 全报错 |
| example | 179 | ✅ | ✅ | ✅ | 全好 |

- 失败形态：`[WARNING] NCA Partition 0 not readable. (AES-CTR Key was not determined)`（A7 矩阵实测复现）。
- prod.keys：BOTW 全走 titlekey 路径 → master_key 数量（17→23）无关；`nca_header_key` 缺失可由 `nca_header_kek_source`+`nca_header_key_source`+`master_key_00` 派生（nstool 自动）。
- **驱动设计修正**：判型（只需 header key）→ 读出 rights id → **先核对 title.keys 覆盖**再启动长提取；缺失则报 `title.keys 缺少 XXX 的解密条目` 并列出缺的 id。备用路径：nstool `--tik <票文件>`（NSP 自带 tik/cert，随浅提取落盘）。

---

### 3.6 解包驱动实现（`runtime/nsp_extract.c/.h`，2026-11-10）

- 模块边界：不依赖 paths.c/platform 实现——`remove_game_files` 由调用方注入（UI 传 `rt_remove_game_files`），nstool 路径由调用方解析（NULL = 只认 `SAVAGEWIND_NSTOOL` 环境变量，开发者指向 `<ws>\nstool\bin\nstool.exe`）；平台函数仅 plat_spawn/wait/kill/remove_tree + plat_now_ns/wait_until_ns。测试链接零平台实现（stub 全套 + stub nstool 写出假产物），51 项全绿。
- AES 拆分：`runtime/aes128.{c,h}` 共享 AES-128（FIPS C.1 + NIST SP 800-38A 双向量锁定双向）；ncz.c 改为调用方（`ncz_aes128_block` 保持测试钩子），ncz 17 项 + 黄金回归全绿。
- NCA 头解析实测定稿（`nspe_parse_nca_header`）：**字段区在 0x200**（两段 RSA 签名之后），分区表 0x240、四个 0x200 fs 头在 0x400——与 libpietendo 结构体注释（0x400）差一签段；探针 `test/work/nca_probe3.py` 对 staged NCA 逐字段验证。加密区 = 扇区 1..5（0x200..0xC00），`nspe_xts_decrypt` 按 tc::crypto 约定（tweak = 8 零字节 || BE64(sector)），向量 `tools/test_xts_vector.inc`（`test/work/xts_vector.py` 生成，pycryptodome 对拍）。
- 判型规则（照 §3.3）：Program + 任一分区 AesCtrEx = 更新；Program 其余 = 本体；PublicData 且 `progid & 0xFFF` ∈ 1..8 且 `(progid>>12) == (app>>12)+1` = aocN（BOTW E000 → F001/F002）；Meta/Control 跳过。更新缺本体 → NSPE_NO_GAME（合并 romfs 必须双包）。仅 DLC → 不删旧 game files（叠加安装）。
- 阶段 D 先 exefs 后长提取：NSO0 + 0x40 读 build id → game_versions.h 查表（未知/不支持 → NSPE_VERSION）；romfs 落地后核对 `System/Version.txt` 与 exefs 版本一致。
- 进度：nstool 每 250ms 轮询输出目录字节数（上限封顶），exefs 预期 = 分区大小、romfs 预期 = 版本表 romfs_bytes、DLC = content_size；阶段边界推送文案（tr() 键，英文串由 UI 层翻译）。
- 取消：run_nstool 轮询间查 `cancelled()` → kill + 删 staging；取消或失败（且已删过旧文件）→ 再调 remove_game_files 收尾，不留半成品；staging 无条件删除（`<data>/sw_cache/`）。
- 密钥纪律：读 prod.keys `header_key`（32B，去空格）+ title.keys 按 id 查行 + staging 内 `<id>.tik`；日志只含内容 ID/类型/分区布局，无密钥。
- 测试：`make test-nsp-extract`（stub nstool 按 argv 写 exefs/main（NSO0+build id）、romfs/System/Version.txt、aoc1；覆盖 args/nstool/keys/package/cancel/titlekeys/OK/version 错误路径 + 全流程 OK）。

---

## 4. 解包驱动设计要点（实现前的定稿）

- 阶段链：A 浅提取全部包 → B 判型分类 → C 密钥预检 → D exefs 提取 + build id fail-fast（1.9.0 不在表 → 明确报版本）→ E romfs/aoc 提取（直接落最终位置）→ F 清理 staging。
- 取消：plat_spawn 起的 nstool 进程纳入 gamebuild 同款跟踪，Esc/B → kill 进程树 → 删 staging。
- 进度：nstool 无进度输出（`Saving ...` 行有文件名）→ 按已提取字节/预估总量近似；阶段间刷新文案。
- 失败即停（fail fast）：已删 staging；错误码进 i18n。
- Windows 长路径：staging 放 data 根下短名目录（如 `<data>/sw_cache/`），避免路径超长。
- GAME_FILES[] 允许名单新增解包产物条目 + `.gitignore`。
- NSZ/XCZ：`.ncz` 条目在浅提取后由 `ncz_rebuild` 还原成 `.nca` 再走判型；staging 需预留 ~NCA 大小的额外空间（单个 988MB）。

---

## 5. 已知约束与风险清单

1. 上游策略（永不上游、日志无密钥）——见 §0。
2. nstool 行为：警告非致命；info 阶段补丁 NCA 必有一条 base 警告（勿当错误）；Sparse Storage 不支持（BOTW 不受影响）；`--titlekeyset` 不传则回落到 `$HOME/.switch/`（Windows = `%USERPROFILE%\.switch`，勿依赖）。
3. nstool 无进度输出；无 `--quiet`；错误通过非零退出码传递。
4. NCZ：仅 solid（含分块 solid）；块表变体明确拒绝；GPL nsz 工具代码禁用。
5. fmt v10 consteval：nstool 全量重建时三处补丁缺一不可（§3.1）。
6. 整合包双 Program NCA 非 AesCtrEx 区分；Meta ProgID 是补丁 TitleID，勿用配对键。
7. 测试脚本专有：MSYS2 参数转换、pwsh 引号、CRLF（§3.1 备忘）——不影响 SavageWind 本体（CreateProcess）。
8. `docs/PROGRESS-SIMPLIFIED.md` 曾因编码转换损坏（UTF-8 被按 GBK 二次编码）；2026-11-10 已重写为干净 UTF-8，后续编辑请保持 UTF-8。

---

## 6. 故障速查表

| 症状 | 最可能根因 | 章节 |
|---|---|---|
| `Partition X not readable. (AES-CTR Key was not determined)` | title.keys 缺该 rights id 条目（keyset 覆盖面） | §3.5 |
| `undefined reference to WinMain` | 重编 nstool 少了 `-municode` | §3.1 |
| fmt consteval 编译错误 | 三处 fmt v10 补丁缺一 | §3.1 |
| NCZ 重建后哈希不符 | ①条目布局读错（64B）；②CTR 块号不是绝对 `offset>>4`；③**AES 实现本身有错**（用两个已知答案向量核对） | §3.4 |
| AES 已知答案通过但重建仍错 | S 盒个别字节错（如 `sbox[1]`），单向量覆盖不到 → 换多种 key/块验证 | §3.4 坑 1 |
| zstd 流中途失败 | 帧结束后未继续处理后续帧；或截断保护误报 | §3.4 坑 2 |
| `NCZ_E_IO` 且哈希/大小可疑 | 输出文件用了 `"wb"`（只写）却要读回重加密 | §3.4 |
| 补丁 RomFS 提取报 base 缺失 | 忘了 `--basenca` | §3.3 |
| DLC 提取为空/只有 AocVersion | 用错分区索引（DLC 是 `/0/` 不是 `/1/`） | §3.2/§3.3 |
| 本体/补丁分不清 | 没用 AesCtrEx 判别，误用 Meta 的 ProgID | §3.3 |
| MSYS2 测试脚本参数怪异 | ARG_CONV_EXCL / pwsh 引号 / CRLF | §3.1 |
| 合并 RomFS 数目对不上 41062 | base/patch 内容 ID 不配对（不同版本混包） | §2 |

---

### 3.7 UI 集成（`runtime/ui/launcher.mm` + settings，2026-11-10）

- **settings 三键**：`setup_packages[1024]`（`;` 分隔路径）、`setup_prod_keys/setup_title_keys[400]`；load 解析缓冲放宽到 `line[1280]/val[1104]`（`%1103`），save 仅非空时写出行（旧 ini 无这些键也兼容）。存放的是选择本身，不是凭据内容。
- **setup 页**：目录行之后新增"From game packages"分区（`!g_confirm_delete` 才显示，busy 时全部禁用）：Game packages（`SDL_ShowOpenFileDialog` 多选，过滤器 `nsp;xci;nsz;xcz`）、prod.keys、title.keys（单选，`keys` 过滤），三个 Checkmark 复用 `check_left_of_value`；"Set up from packages" 在包+prod.keys 就绪前 `opt_disable_next()`。SDL 对话框回调在任意线程：先落到 `g_pkg_picks/g_prod_pick/g_title_pick`（`g_lock` 保护），UI 线程每帧 `apply_setup_picks()` 折叠进设置并 `settings_save()`。
- **package_thread**：镜像 extract_thread 的尾部——驱动 `nsp_extract_run`（nstool 走 `rt_resource("nstool/nstool" PLAT_EXE_SUFFIX)`，空则回退 `SAVAGEWIND_NSTOOL`；staging 固定 `<数据根>/sw_cache`；`remove_game_files` 注入 `rt_remove_game_files`）→ `build_game_code()` → check 块（check_game_files + rt_dlc_check + run_precompile）→ ready 文案。
- **dlc_only 判定**：运行前后 `stat(exefs/main)` 的 mtime+size 都不变 → 只加了 DLC（驱动未动本体文件），跳过重编译与预编译；否则按全量设置走。
- **进度条映射**：驱动 fraction×1000 写 `g_done/g_total`、`g_copy_skipped=true` → 提取阶段占条的前 10%，建码阶段走满条（与目录流程观感一致）。
- **取消**：busy 覆盖层对包流程显示专属标题与"停止即自毁"提示，Esc/B 置 `g_pkg_cancel`（atomic），驱动轮询到后按 CANCEL 收尾并二次清除。
- **失败映射 `pkg_failure()`**：NSPE_NSTOOL/KEYS/TITLEKEYS/PACKAGE/NO_GAME/NCA/VERSION/IO/CANCEL → 各自的 headline + 处理建议（TITLEKEYS 的 why 是缺失 rights id 列表，由驱动直接给出——文案语义放 UI，驱动只报数据）。
- **本机构建环境**（E2E 前置）：MSYS2 UCRT64 原本无 clang/SDL3/ffmpeg；已装 clang 23.1.5 + lld + llvm + gcc-libs 16（官方源 repo.msys2.org 自洽拉取；国内镜像 DB 与包版本不同步会 404/416，镜象列表已前置官方源）；`test/../deps/` 前缀（SDL3 mingw64 + ffmpeg ucrt64 + vulkan headers，`make DEPS=../deps` 即可编译 app）。launcher.mm 经 `clang++ -fsyntax-only`（真实 SDL3/ffmpeg 头）零错误。

### 3.8 nstool 打包（2026-11-10）

- **决策：不提交二进制**。nstool（jakcron/nstool，MIT）从兄弟 checkout `../nstool` 构建（`NSTOOL_SRC ?= ../nstool`，与 DLSS_SDK 同款覆盖模式），与 zig（tools/fetch_zig.sh）、FSR DLL（tools/fetch_fsr_dlls.py）的"构建/获取时进包"模式一致；仓库只留配方不留产物。兄弟 checkout 的 simplified-build 分支改了三处：makefile C++20 + `-municode`、CnmtProcess 的 fmt v10 consteval 修复、libtoolchain 同款修复，并且**重新启用了 `--titlekeyset`**（上游已禁用；驱动的 title.keys 流程依赖它）。
- **Makefile**：`$(B)/nstool/nstool.exe` 目标 = 兄弟仓库增量 `make -C $(NSTOOL_SRC) CC=gcc CXX=g++ -j8` + 拷贝 `bin/nstool.exe`；`make dist` 里 `$(MAKE) $(B)/nstool/nstool.exe` 后放入 `$(DIST)/nstool/nstool.exe` + `LICENSE`（MIT 原文）。运行时解析：`rt_resource("nstool/nstool.exe", NULL)` 查 `<exe目录>/nstool/nstool.exe`（paths.c:148），与 dist 布局一致；开发构建回退 `SAVAGEWIND_NSTOOL`。
- **验证**：目标实跑通过（兄弟仓库增量后拷出 11.2MB）；`make -n dist` 配方展开正确；包内二进制 `--help` 显示 `--titlekeyset`。README 的"release zip 附带组件"清单补 nstool 行（MIT、fork 说明）。

### 3.9 本机 app 构建与运行（2026-11-10 实测，E2E 前置）

- **链接修复**：`runtime/ncz.c` 落在 `RT_SRC = $(wildcard runtime/*.c)` 之后，app 链接却只给测试目标带了 `third_party/zstd/zstddeclib.c` → 首次全量 app 构建报 `ZSTD_*` 未定义。修法：`$(B)/zstddeclib.o` 规则 + 加入 `$(EXE)` 前置（Makefile）。
- **运行库必须走 `make deps-windows`**：MSYS2 的 ffmpeg 包是全量 GPL 构建（avcodec 静态引入 x264/x265/aom/glib/cairo 等 40+ 外部 DLL，删不干净），本地跑 app 和 `make dist` 都用 `build/deps-win` 的 LGPL 精简构建（`DEPS=$PWD/build/deps-win`；源码 tar 经镜像/断点续传校验 sha256 后放入 `build/deps-win/src/`）。slim 只有 4 个 DLL，静态导入闭包干净。
- **SDL3 必须用 UCRT64 版**（`mingw-w64-ucrt-x86_64-sdl3`）：MSVCRT 环境的 mingw64 版 SDL3 在本机 `SDL_LoadObject` 连 `kernel32.dll` 都加载失败（loadso 路径问题），UCRT64 版正常。混用两个环境的 SDL3 是隐蔽陷阱。
- **vulkan-1.dll 不入包、不 beside exe**：驱动在 System32 自装（dist 同理）；MSYS2 的 loader 拷贝反而在 SDL 的 `LoadLibraryEx` 下报 proc-not-found。
- **冒烟验证**：`SAVAGEWIND_LAUNCHER_SHOT=<帧>:setup` + `SAVAGEWIND_SHOT_PATH=<绝对路径>` 可无头截图 setup 页（相对路径以数据根为基准，会落到 `build/build/`）。实测截图确认："从游戏包"分区（游戏包 / PROD.KEYS / TITLE.KEYS 三选择器 + 置灰的"从游戏包设置"）+ zh-Hans 文案全部正确渲染。
- **本地数据根**：直接 `cd build && ./savagewind.exe` 会把 build/ 当数据根（生成 extracted/、sw_cache/ 等）；E2E 用独立目录启动。

### 3.10 真实 dump E2E 结果（2026-11-10，NSP 三模式）

- **输入**（用户自备 dump）：本体+更新整合包 `[01007EF00011E000][v1114112] (1G+1U).nsp` 15.47GB + DLC1 `[01007EF00011F001][v196608].nsp` 1.39GB + DLC2 `[01007EF00011F002][v196608].nsp` 0.12MB（空壳，内容仅 `System/AocVersion.txt`）。
- **提取产物**：`extracted/` = 48,162 files / 15.41GB（romfs+exefs）；`aoc1` = 7,094 files / 1.29GB；`aoc2` = 1 file。DLC 检查 ready（aoc1+aoc2, version "3.0"）。
- **游戏代码**：生成器（venv python + tools/gen.py）+ clang 编译，407s（559/1059 片段命中 cache/gamecode）→ `swgame.dll` 227,978,935 B，`.id` = `dcb63f3167a4a698`（与 exe 内置 GAMELIB_ID 一致）。
- **启动验证**：`[gamelib] extracted/swgame.dll (game version 1.9.0, 160 sites)`；main 125,144 + subsdk0 13,936 + sdk 40,516 函数；台地 warp 后 60.0 fps、~400k draws/2s。
- **编译器缺失的坑**：Windows 上 `find_tools` 只认 `SAVAGEWIND_CC` → `zig/zig.exe`，**无 clang 回退**（该回退仅 `#ifndef _WIN32`）；开发布局必须设 `SAVAGEWIND_CC=<clang>` 或放置 `zig/`（`tools/fetch_zig.sh`）。否则 `build_game_code()` 立即失败，界面显示 FAIL_BUILD 的通用文案（"请确保有足够的可用空间…"），极易误导——已修：FAIL_BUILD 现在把 `gamebuild_run` 的 `why`（如 "the game code builder is missing from this copy of Savage Wind"）前置到提示里（launcher.mm `g_fail_detail`）。
- **zig 玩家路径（同日补验）**：`zig/` 0.17.0 置于 exe 旁（sha256 校验通过）→ 全量 zig cc 编译 487s（0 片段复用；缓存按编译器区分）→ swgame.dll 222MB → 加载 + 台地 60fps / ~40 万 draws（与 clang 构建一致）。zig 下载在本网络被单连接限速 ~16KB/s，用 IDM 式多连接或浏览器+代理解决；注意带存档后标题流程不同，无头 warp 测试应配空 `SAVAGEWIND_SAVE_DIR`。


## 7. 变更记录

| 日期 | 事项 |
|---|---|
| 2026-11-10 前 | 项目克隆；nstool 五依赖构建；fmt×2 修复（2236bb9 / adeb4cd） |
| 2026-11-10 | makefile 适配+构建脚本（0898c00）；gitattributes（4b03518）；`--titlekeyset` 启用（3590b3d）；`bin/nstool.exe` 产出 |
| 2026-11-10 | CLI 实测收官：NSP 三模式、`--basenca` 合并对账、DLC、XCI 等价、NSZ 分块变体解码、9 组 key 诊断（A1–A7/B1–B3） |
| 2026-11-10 | 本文档建站 |
| 2026-11-10 | §3/§4 步骤完成：`third_party/zstd/` vendor（1.5.7 解码合并源）、`runtime/ncz.c/.h`、`tools/test_ncz.c`、`make test-ncz`。合成 17 项全绿；真实 988MB NSZ 条目重建后 sha256 = `94a679f1…`，与原始 NCA 字节一致（黄金验收 ✅）。修三个实现级 bug：S 盒越界、输出 `wb+`、zstd 截断误判 |
| 2026-11-10 | 修复本文档双重编码损坏（重写为干净 UTF-8），并补 §3.4 最终语义、§5.8 编码约束、§6 新故障行 |
| 2026-11-10 | §5 步骤完成：`runtime/nsp_extract.c/.h`（A–F 全链路 + NSPE_* 错误码）+ `runtime/aes128.{c,h}`（共享 AES-128 双向）+ `make test-nsp-extract`（51 项合成，stub nstool 全流程）+ XTS 已知答案向量 `tools/test_xts_vector.inc`。NCA 头偏移实测定稿（字段区 0x200，见 §3.6）；ncz 17 项 + 黄金回归全绿 |
| 2026-11-10 | §6 步骤完成：launcher.mm"从游戏包"setup 分区 + settings 三键持久化 + Esc/B 取消 + NSPE_* 失败文案映射（§3.7）；驱动 TITLEKEYS 的 why 改为纯 id 列表；clang 工具链 + `deps/` 前缀就绪（`clang++ -fsyntax-only` launcher.mm 通过；clang 下 nsp-extract 51 + ncz 17 回归全绿） |
| 2026-11-10 | §7 步骤完成：38 个新键 × 13 语言（de es fr he it ja ko nl pt ru th zh-Hans zh-Hant）+ 8 个驱动步骤名入表；SDL 文件对话框过滤器说明走 tr()、扩展名模式 `// i18n-skip`；旧迎宾键全表移除；`tools/i18n_check.py` 全语言 0 缺失（exit 0；8 个驱动步骤名因扫描器不读 nsp_extract.c 显示 unused，属预期） |
| 2026-11-10 | §8 步骤完成：`$(B)/nstool/nstool.exe` 目标（兄弟 checkout 增量构建，不提交二进制）+ dist 打包 `nstool/nstool.exe` + MIT LICENSE + README 组件行；包内 `--titlekeyset` 验证可用（§3.8） |
| 2026-11-10 | 步骤 9 前置完成：本机 app 全量构建打通（clang + `deps/` 前缀），修 `$(EXE)` 缺 zstddeclib.o 的链接 bug；`make deps-windows` 的 LGPL ffmpeg 就位；SDL3 换 UCRT64 版后 Vulkan 加载正常；setup 页无头截图实测通过（见 §3.9） |
| 2026-11-10 | **步骤 9 E2E 通过**：真实 NSP 三模式（整合包+DLC1+空壳 DLC2）→ 提取 48,162 文件 + aoc1 7,094 文件 → 生成器+clang → swgame.dll 227MB → 进世界 60fps。同时修 FAIL_BUILD 提示缺真实原因的问题（`g_fail_detail` 前置），见 §3.10 |
