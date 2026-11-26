# MRP 应用内 EXT 静态研究与 ESP32 替换评估

## 1. 范围和结论

本报告针对 `F:\workspace\mrps` 中的 664 个顶层 MRP，按包索引枚举**直接包含的** `.ext`，不包括嵌套 MRP 内部、外部文件、其他目录或运行时插件。扫描只读取文件、解压和静态反汇编；没有执行应用或 EXT，也没有访问网络、短信或支付功能。输入是用户本地语料，不随仓库提供。

- 619 个包包含直接 EXT；45 个没有。没有文件不代表没有动态或外部依赖。
- 共 2,115 个 EXT 索引条目，解压后按 SHA-256 去重为 **679 个不同内容**。按“小写名称 + 内容哈希”分组为 **680 组**，不同名称可能有相同内容。
- 2,115 个条目均成功通过 gzip 解压校验，解压后均以 `MRPGCMAP` 开头。这是此语料的结果，不是所有 EXT 的通用标准。
- 所有 619 个包都有应用内 `cfunction.ext`；没有一份的解压内容哈希等于 mrpx 的[平台 Guest](../../assets/runtime/cfunction.ext)。同名不代表相同用途。
- 未在 680 个名称/内容组的解压字节中找到精确字节序列 `7f 45 4c 46`、`.symtab`、`.strtab`、`.debug_`。**这不是证明没有其他格式的符号、字符串、代码或调试信息**；压缩、加密、非标准布局等均可能影响判断。
- 高频 `mrc_loader.ext` 的一个版本可以静态定位注册回调和读取 `cfunction.ext` 的路径，是值得优先研究的替换候选；**还没有动态验证，也没有完成 ESP32 替换**。

包内文件存在、字符串命中、反汇编能解码，都不等于代码一定执行或功能已确认。原始包的功能、兼容性与授权均须分别核实。

## 2. 全量名称统计

包数按同名 EXT 在一个包中只计一次；条目数保留同名重复索引；版本数按**解压内容 SHA-256**去重。文件名只是一条用途线索。

| 名称 | 包数 | 条目 | 版本 | 解压大小范围（字节） | 静态判断与替换优先级 |
|---|---:|---:|---:|---:|---|
| `cfunction.ext` | 619 | 619 | 199 | 368–340,140 | 名称相同但内容差异大；不可统一替换 |
| `game.ext` | 443 | 443 | 432 | 5,188–380,120 | 多数内容不同；不能假定是公共模块 |
| `verdload.ext` | 426 | 426 | 8 | 17,416–20,988 | 重复较多；加载器用途仅为名称线索 |
| `mrc_loader.ext` | 361 | 399 | 4 | 212–388 | 高频、体积小；首个分析候选，见第 4 节 |
| `graphics.ext` | 98 | 98 | 6 | 8,148–11,172 | 可先调查图形相关接口；功能未验证 |
| `skyfont.ext` | 70 | 70 | 4 | 5,820–5,984 | 可先调查字体相关接口；功能未验证 |
| `pp.ext` | 24 | 24 | 8 | 8,276–9,424 | 功能未知 |
| `advbar.ext` | 13 | 13 | 3 | 50,912–54,996 | 功能未知 |
| `hgame.ext` | 9 | 9 | 2 | 8,028–9,364 | 功能未知 |
| `plugin.ext` | 2 | 2 | 2 | 38,352–56,324 | 功能未知 |
| `simlink3d.ext` | 2 | 2 | 2 | 17,588–17,684 | 功能未知 |
| `advgdi.ext` | 1 | 1 | 1 | 16,564 | 功能未知 |
| `brw.ext` | 1 | 1 | 1 | 124,856 | 功能未知 |
| `core.ext` | 1 | 1 | 1 | 24,544 | 功能未知 |
| `freecurr.ext` | 1 | 1 | 1 | 32,076 | 功能未知 |
| `mge_graphics.ext` | 1 | 1 | 1 | 4,872 | 功能未知 |
| `recommend.ext` | 1 | 1 | 1 | 20,100 | 功能未知 |
| `servassist.ext` | 1 | 1 | 1 | 47,000 | 功能未知 |
| `smcheck.ext` | 1 | 1 | 1 | 14,848 | 功能未知；不执行潜在短信行为 |
| `smsend.ext` | 1 | 1 | 1 | 3,996 | 功能未知；不执行潜在短信行为 |
| `smspay.ext` | 1 | 1 | 1 | 71,348 | 功能未知；不执行潜在支付行为 |

包数不能跨行相加：同一个包通常包含多个 EXT。其中 128 个包的直接 EXT 名称集合只有 `cfunction.ext`；这不意味着它们是纯脚本或内部功能相同。361 个含 `mrc_loader.ext` 的包**都还含其他直接 EXT**，其中 313 个也含 `game.ext`。因此仅替换加载器并不能让这 361 个包摆脱全部原生代码。

## 3. 格式、入口和静态线索

对每个名称/内容组的代表文件，用 Capstone 5.0.6 按小端 ARM 模式从偏移 `8` 解码首 20 字节；680 组均能解出 5 条 4 字节指令。任意数据也可能被解码成指令，因此这项统计**不能**证明全部文件的完整指令集、可达路径或执行成功。

高频 `cfunction.ext`、`verdload.ext`、`graphics.ext`、`skyfont.ext` 的各一个代表版本，其入口均出现 ARM `blx`；目标模式和后续代码需分别验证。常见 `mrc_loader.ext` 入口则通过函数表间接调用；第 4 节给出具体偏移。

另外在解压内容中扫描长度至少 4 字节的可打印 ASCII 片段，按不区分大小写的子串标记。`sms`、`pay` 等在不少版本中出现，但可能只是资源名、日志或数据，**不是符号表、调用关系或实际支付行为的证据**。所有标记和首入口助记词见本地 `ext-static-versions.csv`；未尝试据此还原第三方应用源码。

归档 [Mythroad 运行时](mythroad/mythroad.c)的 `_strCom(800)` 将 EXT 数据起始前 4 字节写为函数表指针，并以数据地址加 8 为入口；801 则调用已注册回调。[函数表定义](mythroad/mythroad.c)的槽 0 是内存分配、25 是 `_mr_c_function_new`、125 是 `_mr_readFile`。这是**归档实现的 ABI 对照**，不能仅凭它证明当前平台 Guest 与归档二进制完全一致。

## 4. 高频 `mrc_loader.ext`：一条可追踪的路径

### 4.1 版本与重复索引

| 解压 SHA-256 | 大小 | 索引条目数 | 包数 |
|---|---:|---:|---:|
| `d36151ee3c119717305afe4b1f0ba47f0f0154f8ba6f2c5081d6402c8eddd938` | 232 | 369 | 335 |
| `cd3efc6f3f8dfc624e141777f248de47e2ae2bde5c1b5530c6ffc1c0dc800a80` | 212 | 24 | 22 |
| `bedc16a8dbbfb646269bdfd5410163e45c1833b3aea588add6cc7393c3c22f58` | 212 | 5 | 5 |
| `62f4cfb710e9a795cdb9e549c32cc076c502fc72f59e366602c75619aee1470e` | 388 | 1 | 1 |

版本包数不能相加：两个包的重复同名索引含不同版本。按索引中的**首个精确名**统计，361 个包分别是 `d361...` 334、`cd3e...` 22、`bedc...` 5；`62f4...` 出现在重复项的第二项。运行时实际请求的名称、外部同名文件优先级和调用路径仍可能改变结果，不能据此认定 334 个包都执行了该版本。

### 4.2 `d361...` 代表样本

样本为 `1020_240320_skirt.mrp`，包 SHA-256 `fd3436a8724f5097daea85199e5e3d267263a2ea353862e9fa064982f0cd72d8`；EXT 为索引第 2 项，解压 SHA-256 见上表。偏移均相对于解压后的 EXT 起始：

1. `[静态反汇编]` `0x08` 入口读取偏移为 `-8` 的函数表指针；`0x1c` 通过表偏移 `0x64` 取函数，即 32 位表槽 25。
2. `[静态反汇编 + 归档 ABI]` 入口向该槽传入指向 `0x44` 的回调和长度 `20`。归档表槽 25 是 `_mr_c_function_new`，其源码会分配状态并注册回调。入口返回 `3`；其实际调用方对该值的解释尚需验证。
3. `[静态反汇编]` `0x44` 回调调用表槽 0 分配 8 字节，随后直接写入清零，未见空指针检查；回调 `code == 1` 时，通过表偏移 `0x1f4` 即槽 125，传入资源名 `cfunction.ext`、长度指针和 `lookfor=0`。该资源名字节位于 `0xd4`；归档槽 125 对应 `_mr_readFile`。
4. `[静态反汇编 + 归档 ABI]` `code == 1` 分支将读取指针及长度放入 8 字节结果，并设置 `output` 与 `output_len=8`；回调返回 `0`。32 位指针解释依赖对应 ABI；非 `1` 分支的所有权和其他副作用尚未验证。

以上是**可达分支候选的静态解释**，不是一次实际调用的观察；不证明加载器负责运行 `cfunction.ext` 后的全部工作，也不证明其余版本功能完全相同。其余三个版本的入口同样显示槽 25 注册，但状态长度分别为 20、16、20；不能把 16 和 20 字节状态视作可互换。

`d361...` 的可移植候选是复现已确认的资源读取/回调契约，而不是把 ARM 字节改名或直接塞进 ESP32。特别要验证指针宽度、状态大小、内存所有权、读取失败、外部文件优先级、返回码和事件顺序，再决定是否替换。

## 5. ESP32 研究优先级与验收

1. **`mrc_loader.ext`**：为 `d361...` 做受控调用实验，覆盖注册、`code=1`、文件存在与缺失、输出释放；再分别测试其他版本。先按**具体哈希和 ABI**识别，不按名字通配。
2. **公共候选**：对 `verdload.ext`、`graphics.ext`、`skyfont.ext` 的高频哈希建立调用与参数证据；当前名称和重复度均不足以确定实际功能。
3. **`cfunction.ext` / `game.ext`**：选有权限分析的代表包，确定脚本到 EXT 的真实调用链；同名异内容多，不能承诺公共替代能覆盖全部游戏。
4. **对照验证**：原版本在受控、禁用外部网络与短信等副作用的环境执行，记录输入、函数表调用、返回值、输出和生命周期；对照 ESP32 替代实现逐项比较，并验证应用启动、绘图及输入。环境隔离措施和执行权限必须先确认。
5. **失败策略**：遇到未知哈希、未实现的接口或不兼容状态应明确报告不支持；不能返回伪成功、删掉 EXT 或把未验证的行为当作兼容。

静态去重的覆盖数是研究工作量线索，不是可替换数量或游戏兼容率。直接重打包含原 ARM EXT 的 MRP 与在 ESP32 原生执行它们是不同问题；前者不消除后者的指令和 ABI 障碍。

## 6. 数据和复核方法

本地输出位于 `build\mrps-ext-inventory-20261126`（`build` 是忽略目录，不随仓库提交）：

| 文件 | 作用 | 文件 SHA-256 |
|---|---|---|
| `ext-entries.csv` | 2,115 条包名、索引、大小和解压哈希 | `d65a4c340ab24b6cbac35ba8016a4419e155c79b36d24dc35024ffb864545702` |
| `ext-versions.csv` | 680 个名称/内容组及代表包 | `2987abbdeae3eedc147128e5bb16831946127ba3eb803b18bafd3a10fa32a258` |
| `ext-static-versions.csv` | 每组入口助记词和字节/字符串标记 | `55ff5bdfe08ef52c627da6f352f3c060d3cfc1cfd89f5c481ab64e40ce4f804d` |
| `mrp_catalog.csv` / `mrp_catalog.json` | 全部 664 包的索引和扫描信息 | 由扫描工具生成 |

扫描使用 [mrp_tool.py](../../tools/mrp_tool.py) 的 `scan`；后续逐条重新读取包、校验包 SHA-256，对每个直接 EXT 解压并计算内容 SHA-256。按名称/哈希选代表版本，逐组检查字节标记和入口；第 4 节另检查相对地址与回调路径。统计使用完整索引，保留重复名称。52 个外层包含 82 个嵌套 MRPG 资源，**没有递归分析**；这 52 个外层包均已含直接 EXT。

从仓库根执行，使用新目录避免覆盖。以下命令独立复核第 4 节的包和 EXT：

```powershell
Get-FileHash 'F:\workspace\mrps\1020_240320_skirt.mrp' -Algorithm SHA256
python tools\mrp_tool.py unpack 'F:\workspace\mrps\1020_240320_skirt.mrp' --output build\ext-loader-repro
Get-FileHash 'build\ext-loader-repro\files\000002.bin' -Algorithm SHA256
```

完整重扫可使用 `python tools\mrp_tool.py scan F:\workspace\mrps --output build\ext-catalog-repro`；扫描器提供包级 EXT 名称、索引和解压校验，名称/内容哈希去重及本报告的字节扫描是额外的离线分析，不由 `scan` 单独生成。本次代表样本的 ARM 反汇编使用 Python Capstone 5.0.6，偏移从解压内容起算；第 4 节的计算应结合归档 [mr_helper.h](mythroad/include/mr_helper.h)和运行时函数表复核。

## 7. 历史 SDK 中的符号化目标文件及语料对照

### 7.1 SDK 中确实保留了符号，但并不在本语料的原始 EXT 头部

`[离线实测]` `F:\workspace\MythroadSDK-master\compiler2\mr_helper.lib` 是静态库；其中 `mr_helper.o` 是 32 位、小端 ARM ELF 可重定位对象，含 `.symtab`、`.strtab`、函数节、重定位和部分 `.debug_frame`。GNU `nm -g` 在整个库中列出 230 个不同的已定义 `T` 符号，包含 `mr_c_function_load`、`mr_helper`、`mrc_extLoad` 等，也有未定义符号。库 SHA-256 为 `77a78b2a896631e38e079acdd9c579a3c6ba4910a5481d033586f2eb6f8443a4`。

这与第 3 节的结果不冲突：本批解压后的 `MRPGCMAP` EXT 未发现所搜索的 ELF/符号表字节；这里的符号来自**独立的 SDK 静态库**。SDK 符号能给代码片段提供命名线索，但不等于任意游戏 EXT 都保留符号，也不等于可以直接恢复完整源码。

`[归档文献]` `F:\workspace\mrpdev-master\MRP模拟器解决方案.TXT` 的 SHA-256 为 `11f95226bc580ee305e70c085f738db39a80c1c43c375147adf5810c12926412`。该社区说明将 `mr_helper` 描述为消息分发器、将表偏移 `0x64` 描述为注册函数，并记录一种以 `mr_c_function_load` 为链接入口的历史构建流程。它的开头又将偏移 8–11 描述为“函数指针”，与归档实现直接执行 `EXT+8` 的代码及本次反汇编存在表述差异；不能把整份说明当作已验证 ABI。

### 7.2 通用入口的四个 SDK 对照与用户语料

`[离线实测]` 从上述 `mr_helper.o` 的 `i.mr_c_function_load`、`i.mr_helper` 两个函数节各取**从节首开始的 32 字节**，在解压 EXT 中搜索原样字节。两个片段都在 SDK 的 `samples\Thumb\demo.mrp`、`demo_thumb.mrp`、`demo_font.mrp` 和 `demo_thumb_font.mrp` 中命中；入口片段均位于 EXT 偏移 `0x08`。这四个包是正向对照，不证明当前源码就是其构建输入。

对第 1 节的 680 个名称/内容组使用同样的两个片段：

| 静态命中情况 | 名称/内容组 | EXT 条目 | 包 |
|---|---:|---:|---:|
| 入口与 `mr_helper` 两个片段都命中 | 145 | 145 | 145 |
| 仅 `mr_helper` 片段命中 | 11 | 11 | 11 |
| 两个片段都未命中 | 524 | 1,959 | 不以此推断功能 |

所有命中均位于应用内 `cfunction.ext`；145 个入口片段均在偏移 `0x08`。例如本语料 `winmine.mrp` 的该 EXT 解压 SHA-256 为 `0ab9d63ce76cd2e6f6ab34fc27561e6fc8ca94562e842525733d6ddbcef4dd48`，两个片段分别在 `0x08`、`0x2950`。这为它与 SDK 通用入口存在**局部字节相同**提供依据；不证明其余函数相同，也不能据此将整个 EXT 等同于 helper 库或安全替换。

这只是两个固定短片段，不匹配可能来自不同版本、ARM/Thumb 编译、链接重定位、优化或代码变化；匹配也不证明控制流可达或语义完全相同。

### 7.3 字体库的正反对照

`[离线实测]` `F:\workspace\MythroadSDK-master\samples\Thumb\src\MTK_fontModule_thumb.lib` 包含 ARM ELF 对象 `mr_src_fontread.o`，有符号与重定位；`nm -g` 列出 17 个不同的已定义 `T` 符号，例如 `FontModuleInit`、`FontModuleRelease`、`GetUniBitmap`、`_drawText`，以及 23 个不同的未定义 `U` 符号。库 SHA-256 `f99e83bf80116553a7489467acd13271363dfd19c33cbfd05e4e39d842c38c2b`。同目录 `fontRead.h` 给出接口声明；它不是这些函数的实现源码。

SDK 的 `Helloworld-thumb_Font.mpr` 引入该库和两份字体数据，而 `Helloworld-thumb.mpr` 未列入它们。对库中四个函数节各选一个 32 字节片段，在 `demo_thumb_font.mrp` 的应用内 EXT 均找到，在 `demo_thumb.mrp` 的 EXT 均未找到。另对字体库所有至少 32 字节的函数节以 2 字节步长搜索，带字体样本有 8 个节至少存在一个完全相同的窗口；不带字体样本没有命中。该对照支持利用符号化对象寻找相关片段，**但不能仅凭现有工程文件证明这两份历史样本就是由当前文件构建**。

把上述四个固定片段应用到用户语料的 680 个名称/内容组，**均未命中**。仅说明这四个特定字节窗口没有原样出现；不能得出用户包没有字体实现、没有该库变体或不使用字体的结论。链接重定位、不同编译配置及不同 SDK 版本都可能改变匹配结果。

SDK `samples` 下 19 个 MRP 的 18 个直接 EXT 条目共 17 种内容，经解压哈希比对，没有与用户语料 EXT **整文件哈希**相同的项；这与上述局部片段命中并不矛盾。

### 7.4 数据、复核与边界

| 本地文件 | 内容 | SHA-256 |
|---|---|---|
| `build\mrps-ext-inventory-20261126\sdk-helper-fragments.csv` | 680 组中两个 helper 片段的命中偏移 | `0216307023974ca1afaae137bdf1cedb382a36ead217af6a48b0ca4df2e67aad` |
| `build\mrps-ext-inventory-20261126\sdk-font-fragments.csv` | 680 组中四个字体片段的命中偏移 | `eb036de852e00b4f568ba6154476364f391870988f6ab501aabaf78b567f5607` |

上述 CSV 与 SDK 输入都不随仓库提交。片段是每个 ELF 函数节的 32 个连续原始字节：`i.mr_c_function_load`、`i.mr_helper`、`i.GetSupportFont`、`i._drawText`、`i.FontModuleInit` 分别从节偏移 0 取，`i.GetUniBitmap` 从节偏移 8 取。搜索未忽略重定位字节，也未使用模糊匹配；CSV 中的偏移相对于解压 EXT 起始。用户语料每组取哈希校验通过的代表版本，包数再按条目清单去重。

可在有 GNU binutils 的环境中复核文件类型与符号；命令只读取库，不运行库：

```powershell
ar -t 'F:\workspace\MythroadSDK-master\compiler2\mr_helper.lib'
readelf -h 'F:\workspace\MythroadSDK-master\compiler2\mr_helper.lib'
nm -g 'F:\workspace\MythroadSDK-master\compiler2\mr_helper.lib'
nm -g 'F:\workspace\MythroadSDK-master\samples\Thumb\src\MTK_fontModule_thumb.lib'
```

后续应优先对多个有符号函数节建立考虑重定位的候选匹配，再用独立样本、真实调用及边界条件验证；不能把函数名或 32 字节重合作为一键替换依据。本次没有执行任何 EXT、历史编译器或收费功能，也未确认这些归档的完整授权状态。

## 8. `Small_Font` 历史样本的完整 EXT 哈希匹配

`[离线实测]` `F:\workspace\mrpdev-master\Small_Font.rar` 的 SHA-256 为 `fe4f180ea9ac80245cbbccc0166e4f22e82293f9b23af150d6e554a1882e5788`。仅提取了指定的示例源码、工程文件、静态库和四个 MRP，没有执行其中程序。

| 历史样本及 EXT | 解压 EXT SHA-256 | 用户语料中完全相同的条目 / 包 |
|---|---|---:|
| `res\skyfont_mtk.mrp` 的 `skyfont.ext` | `c98ad1aab18c256e3725556c00daf082205c854ea64cddd109b287f75d9fa23a` | 10 / 10 |
| `mrp\mtk_font.mrp` 的 `skyfont.ext` | 同上 | 同上，不重复计数 |
| `mrp\mtk_font.mrp` 的 `verdload.ext` | `374445febaf94de6f126e74814782268c94aefc0fa5425a065ec7a030a4bb214` | 224 / 224 |

上述 10 个 `skyfont.ext` 包均在 224 个 `verdload.ext` 包之内，因此不是 234 个不同包。`skyfont_sp.mrp` 和 `spr_font.mrp` 中的对应 EXT 未发现与用户语料完全相同的哈希。完整匹配只说明**解压字节相同**，不证明这些包实际加载了文件、功能的全部语义或不同设备的行为。

参考包 `skyfont_mtk.mrp` SHA-256 为 `8c7de013fd00cb5e92fb773eed9f6a3cc58c321e691dc8c6a0691bdd8f3ae643`，`mtk_font.mrp` 为 `5994584efeb3c46212e43f1669fcadbc31796c919dbe3649666d7663e173f9e8`。归档中的 `project\all.mpr` 将 `skyfont_mtk.mrp`、`verdload_mtk.mrp` 列为资源，并列出 `skyfont_mtk.lib`、`verdload_mtk.lib`；但没有证据证明这些现有包由此工程文件和当前库构建。

`[源码和符号线索]` 同一归档的示例 `src\small_font.c` 调用 `mrc_skyfont_load`，在回调成功后调用 `mrc_skyfont_textWidthHeight` 和 `mrc_skyfont_drawTextLeft`。`lib\skyfont_mtk.lib` 是包含 ARM ELF 可重定位对象的静态库，其全局已定义符号包括这三个名称及 `mrc_skyfont_unload`，并引用 `mrc_extSendAppEventEx` 等接口。库 SHA-256 为 `70773451a1ff2f34aeed45fcc86deda521688f38abd505975bc39cf7869e3929`。这为研究**字体插件的客户端接口**提供具体入口，但还未验证这些库函数与匹配的 `skyfont.ext` 之间的完整消息协议、参数布局、生命周期或像素结果；不能将该库直接当作 ESP32 可执行代码。

结果清单为本地 `build\mrps-ext-inventory-20261126\sdk-font-exact-matches.csv`，234 行、SHA-256 `ca06376177c917bc3af05458e34897a7143970357f54356393940ee6f922c4b2`；其中每行记录包名、索引、EXT 内容及包哈希，可按 `ext_name` 区分两种匹配。第 7.3 节的零命中仅针对**另一套**字体库选定的四个 32 字节片段，不与这里的完整 `skyfont.ext` 相同哈希结论冲突。

下一步应先研究该样本的插件消息结构和调用边界，记录字体加载、缺字、换行、测量和释放的结果；在确认运行环境隔离及使用权限后，才能与 ESP32 侧实现比较。`verdload.ext` 的完整字节匹配和工程文件名称是重要线索，**还不足以证明其全部功能或可直接替换**。
