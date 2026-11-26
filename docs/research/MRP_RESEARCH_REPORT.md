# MRP / Mythroad 研究报告

## 0. 证据范围与交付

**实测：**此前对相邻语料目录 `vmrp.github.io-master\public\mrps` 的 664 个文件执行离线解析，没有运行这些游戏、应用内原生扩展或收费功能。每包 SHA-256、逐项偏移和存储大小、压缩校验、头部 CRC 比较、入口常量线索与静态全局引用在 [JSON 档案](../../build/mrp-research-v3/mrp_catalog.json)；[CSV 档案](../../build/mrp-research-v3/mrp_catalog.csv)可直接排序。`build` 是本地生成目录，复现命令见第 8 节。

**源码：**依据仓库内历史实现，不假设它与当前运行中的 Guest 二进制完全一致。格式、运行时、Lua 差异的具体证据分别见 [MRP_FORMAT_SPEC.md](MRP_FORMAT_SPEC.md)、[RUNTIME_SPEC.md](RUNTIME_SPEC.md)、[LUA5_DIFFS.md](LUA5_DIFFS.md)，社区归档的历史手册和 SDK 样本见 [OFFICIAL_DOCS_NOTES.md](OFFICIAL_DOCS_NOTES.md)。本报告是可复查的阶段性研究，非所有历史发行版的权威完整标准。

**动态探针：**使用自写脚本和本仓库 VM/parser 子集完成 32 位位运算实验；另在当前 mrpx WASM 执行了用户源码包及自写方言探针。WASM 使用平台自身的 `cfunction.ext` Guest，但未执行应用内 EXT；这不是运行真实游戏或官方工具。环境、编译器优化异常及复现方法见 [Lua 差异报告](LUA5_DIFFS.md)第 7 节。

**与任务书事实的冲突：**1 个包没有精确名 `start.mr/start.m`，其入口候选为 `_start.mr`；扫描不能支持“664 个均有精确入口”。归档 VM 默认 `mrp_Number=int`，不是默认浮点数。精确名重复的包有 **541** 个；此前报告的 542 是把无精确入口包也算在内的旧统计。

## 1. 全量分类

规则：容器索引中无 `.ext` 且按 `start.mr` 优先、否则 `start.m` 的**首个精确匹配**所解析出的全部 Proto 字符串常量不含 `_mr_c_load`、`.ext`、`cfunction`、`game.ext`、`cfunction.ext`，为 A；任一命中为 B；无法按规则读取或解析入口为 C。分类前先枚举全部资源；非入口资源 CRC 异常单独报告，不改变 A/B。

| 类别 | 数量 | 占比 | 能证明什么 |
|---|---:|---:|---|
| A | 45 | 6.78% | 通过上述静态规则；**不证明可独立运行或没有动态/外部依赖** |
| B | 618 | 93.07% | 文件名或常量存在原生扩展线索；**不证明执行路径一定使用扩展** |
| C | 1 | 0.15% | 无精确入口，不代表文件损坏 |

另外对 A 类包中其他 `.mr/.m` 条目做了关键词静态检查，未发现目标 EXT 关键词；这个附加检查不在 A/B 判定中，也不能排除动态字符串和外部资源。实际兼容率须运行验证。

对 663 个已解析入口的字符串常量额外做不区分大小写的**子串**筛查：`_strCom` 命中 655 包、`dofile` 命中 48 包，`_loadBuf/_loads/_execFile` 均未命中。这些只是标记，既不证明有函数调用，也不自动改变 EXT 分类。

### 1.1 B/C 抽查

以种子 `20261126` 抽 3 个 B，核对索引和入口常量；C 全部复核：

| 包 | 复核 |
|---|---|
| `399152_240320_gxjqxz.mrp` | 包含 `game.ext/cfunction.ext/verdload.ext/hgame.ext`；常量命中 `_mr_c_load/.ext/cfunction/cfunction.ext` |
| `361002_240320_ghdlb.mrp` | 包含 `game.ext/cfunction.ext/verdload.ext/mrc_loader.ext`；常量命中 `_mr_c_load/.ext` |
| `301065_240320_gfdwq.mrp` | 包含 `mrc_loader.ext/game.ext/cfunction.ext/verdload.ext`；常量命中 `_mr_c_load/.ext` |
| `5167448.mrp` | 无精确 `start.mr/start.m`，含 `cfunction.ext` 和 `_start.mr`；后者可解析且引用 `_mr_c_load`、`cfunction.ext`，仍按约定分 C |

按**静态规则**复核的 3/3 没有分类错误；这不是对真实运行依赖的误判率估计，样本太少且未执行任何路径。

### 1.2 A 类按包大小升序 Top 20

| # | 包 | 显示名 | 字节 |
|---:|---|---|---:|
| 1 | `applist.mrp` | 移动视线 | 10,024 |
| 2 | `8412_240320_rxhua26.mrp` | 阿凡提笑话大全 | 22,769 |
| 3 | `8407_240320_rxhua21.mrp` | 笑笑阿凡提 | 24,012 |
| 4 | `8393_240320_rxhua7.mrp` | 国外幽默荟萃 | 25,024 |
| 5 | `8401_240320_rxhua15.mrp` | 最新笑话专集 | 25,082 |
| 6 | `8406_240320_rxhua20.mrp` | 笑笑更健康 | 25,202 |
| 7 | `8411_240320_rxhua25.mrp` | 这样的笑话才好笑 | 25,353 |
| 8 | `8409_240320_rxhua23.mrp` | 老外的搞笑事 | 25,387 |
| 9 | `8396_240320_rxhua10.mrp` | 外国笑话典藏 | 25,467 |
| 10 | `8398_240320_rxhua12.mrp` | 中国笑林大全 | 25,686 |
| 11 | `8410_240320_rxhua24.mrp` | 让你笑喷的笑话 | 25,863 |
| 12 | `8388_240320_rxhua2.mrp` | 不是一般的幽默 | 25,926 |
| 13 | `8397_240320_rxhua11.mrp` | 外国幽默大全 | 26,224 |
| 14 | `8405_240320_rxhua19.mrp` | 笑在现代 | 26,269 |
| 15 | `8404_240320_rxhua18.mrp` | 现代人的幽默观 | 26,405 |
| 16 | `8408_240320_rxhua22.mrp` | 经典国外幽默 | 26,463 |
| 17 | `8387_240320_rxhua1.mrp` | 暴强国外笑话 | 26,862 |
| 18 | `8389_240320_rxhua3.mrp` | 搞笑事件大集合 | 26,868 |
| 19 | `8395_240320_rxhua9.mrp` | 外国笑话 | 27,016 |
| 20 | `8394_240320_rxhua8.mrp` | 开心瞬间 | 27,053 |

按原样排序时最小包是列表组件、另外两个是阅读类，并非已验证的可独立运行小游戏。其体积小不能替代交互和依赖检查。

## 2. 最小三包的资源与静态接口

SHA-256：

- `applist.mrp`: `d92f764bcca62989bfd8a9ffdd1c6f44d56161b2f1ebbdaed46fc7c15d4c1f47`
- `8412_240320_rxhua26.mrp`: `3e5744ef73316ea4b1fdf6a7d4faa0773cccfa8c23f299a29d22c53ede63bfc7`
- `8407_240320_rxhua21.mrp`: `abc94394927e03b6dd5380949fb4c636c03bd9ac7c619b5be055cf65a8e8ad2b`

完整资源列表（大小是解压后，前缀为解压后十六进制；均以 gzip 存储）：

| 样本 | 资源 | 大小 | 前缀/证据 |
|---|---|---:|---|
| applist | `start.mr` | 161 | `1b4d52505001...`，0x50 |
| applist | `list.mr` | 6,954 | `1b4d52505001...`，0x50 |
| applist | `icons.bmp` | 13,312 | `ffffffffffffffff...`，非 Windows BMP 头；未确认尺寸 |
| rxhua26 | `start.mr` | 3,917 | `1b4d52508001...`，0x80 |
| rxhua26 | `menu_rxhua26.txt` | 627 | `5b77696e5d0d0a...`，以 `[win]` 开头 |
| rxhua26 | `rxhua26.con` | 42,983 | `0c7c3030317c0d0a...`，内容数据 |
| rxhua21 | `start.mr` | 3,913 | `1b4d52508001...` |
| rxhua21 | `menu_rxhua21.txt` | 615 | `5b77696e5d0d0a...` |
| rxhua21 | `rxhua21.con` | 42,757 | `0c7c3030317c0d0a...` |

`applist` 入口按 Proto/PC 顺序仅见 GET 全局 `dofile`；`list.mr` 中 GET 全局依次出现 `table`、`list`、`old_table_getn`。它更像应用清单数据，运行前须确认外部环境。

两个阅读包入口按 Proto 递归、各 Proto 的 PC 从小到大，去重后的 `GETGLOBAL` 名称相同：

```text
getRealVer, sys, ReadPackVer, pay_conToNum, _num, Fatal, err, loadPack,
_pCall, dofile, pay_getVersion, getReadpathVer, _exit, _loadPack,
_drawRect, _drawTextEx, _dispUp, TimerStart, string, _strCom, file,
file_open_err_Exit
```

这是一份**静态取值引用顺序**，不保证名称均为函数、均被调用或是动态时间顺序。按名称可初分：绘图 `_drawRect/_drawTextEx/_dispUp`；计时 `TimerStart`；加载 `loadPack/_loadPack/dofile`；文件 `file` 表及常量 `open/seek/read/close`；环境 `sys`、`string`；错误或退出 `Fatal/err/_exit`；`pay_*` 和其他自定义名称为潜在外部/业务依赖，不能假设可安全空实现。未在这些入口中直接 GET `_plat/_platEx`；不排除表调用、别名或其他脚本间接到达。

**逐指令实证：**两个阅读样本的 Proto `0.5` 在 PC 11、18、25 取 `_strCom`，随后均装入 code `600`，偏移分别 68、192、196，长度均为 4；源码中 `_strCom(600)` 读取已注册内存包/RAM 包片段，**不是** `_mr_platEx(600)`。是否实际走到这些分支、依赖的数据由谁提供仍待执行。

没有运行 `luadec`；归档 Makefile 缺少两个引用的源文件，本机 `gcc -m32` 的链接库也缺失，后续 MSVC 实验只构建了独立 VM 子集。**未交付第三方应用的完整反编译源码**：除工具尚未运行外，完整复现受版权保护的应用源码不在本交付中；以上以资源、符号和行为摘要替代。

## 3. 图形与字体交叉验证

除了冠军样本，对 `tetris.mrp`（SHA-256 `46d9f44fc6e51fcc3d6bb46a2c85d3e584b15614edb215c6e2ddc64a3138fd6a`）分析：

- `tetris_n.bmp` 第 12 项还原为 57,600 字节，前 16 字节 `91 eb` 重复；入口 Proto 的 PC 715–723 给出宽 240、高 120、源宽 240。
- `240×120×2=57,600`；按小端 RGB565 和顺序扫描行输出 PNG，人工目视为正常标题图。源码透明模式色键取首像素，复制模式不透明。这证明**该样本**的解释，不证明所有资源。
- `focus.bmp` 参数 198×39，但文件只 15,048 字节，相当于 198×38×2；不能凭长度猜测其显示后果，需调试真实执行路径。
- [gb16.uc2](../../assets/runtime/mythroad/system/gb16.uc2) 为 65,536×32 字节，按 Unicode BMP 码点、16 行×2 字节大端和最高位在左读取。ASCII 数字 0–9、“中”“汉”已做 ASCII-art 目视；不是直接按 GBK 区位号索引。

另一个 `[归档文献+实测]` 字库样本来自 fontmaker 的 `font.mrp`：`fun_N.bin` 用 2 字节字符码加 3 字节点阵偏移索引 `fpoint_N.bin`，每种大小有 208 个 Unicode 项；三套点阵记录均符合各自高度与字宽计算出的紧凑长度。它不是 `gb16.uc2`，且源码注释所写偏移宽度与实际三字节读取相矛盾。样本 SHA-256、字节实例和局限见 [文档笔记](OFFICIAL_DOCS_NOTES.md)。

`[推断/待验证]` 文档的紧凑位排列要求与当前 DSM 为 ASCII 返回的 32 字节固定槽可能冲突。对两条不同条件编译绘字路径作 Python 源码模型比较已出现像素差异，但没有运行真实 VM，不应将模型差异表述为已复现的设备故障。

## 4. 容器、字节码与外围统计

| 项 | 实测 |
|---|---|
| 包魔数 | 664/664 为 `MRPG`；不排除语料外变体 |
| 头部 CRC 字段 | 448 匹配完整物理文件、1 仅匹配声明长度、9 为零、206 未匹配；未知不等于损坏 |
| 索引保留字段 | 96,856/96,856 为零 |
| 可选精确入口 | 663 个均 gzip；`0x80` 662、`0x50` 1 |
| 资源条目 | gzip 验证通过 96,629；gzip CRC 不符 3；不透明/可能 raw 224 |
| raw-deflate | 对 224 项作完整受限探测，成功 0；无签名格式不能由此普遍判定 |
| 嵌套包 | 82 项解压后以 `MRPG` 开头，分布 52 个外层包；不代表自动支持包中包路径 |
| 音频项 | `.mid` + `MThd` 1,148，其他 `.mid` 531；`.wav` + RIFF/WAVE 135；`.mp3` + ID3 54、疑似 MPEG 帧 45 |

CRC 问题是 `box.mrp:box_n.bmp`、`shot.mrp:shot_sea.bmp`、`txz.mrp:backdrop.bmp`；解压长度匹配但内容 CRC 不符。工具默认拒绝、显式恢复才警告继续。历史运行时虽记录错误后返回成功，也不能据此认定数据无损。

664 个包按原始索引/顺序记录重建均**字节相同**；此前 5 个另做了解包 → 打包 → 再解包对照，含异常 gzip CRC 样本。本轮还对 `blockbreak.mrp`、`mrper.mrp`、`applist.mrp`、`winmine.mrp` 分别验证四种头部 CRC 状态的原样及资源修改往返，并对 SDK `helloworld.mrp` 执行同样验证。未知 CRC 的修改包保留原字段且发出警告。这些离线验证本身**没有证明上述语料包在模拟器/真机运行**；下节另外测试了一个用户源码包。

### 4.1 SDK 样本与扫雷源码

`[实测]` 固定提交的 SDK `samples` 下 19 包静态分类 A=1、B=18；79 个资源均通过 gzip 校验，19 个包均匹配 builder 头部 CRC，按原始记录重建字节相同。唯一 A 类 `hello/helloworld.mrp` 已另做解包→打包→再解包；它是归档中现有包，不是本次编译的新产物，也没有运行。见 [SDK 样本档案](../../build/mrp-sdk-samples-v2/mrp_catalog.json)。

`[实测]` 社区归档 `1winmine(正式版).zip` 只有 C 源码和四张 24 位 Windows BMP，没有 Lua 源，因此无法通过简单移除 EXT 调用得到纯脚本扫雷。未运行未签名的历史编译器和模拟器，未产出无 EXT 扫雷；源码图像亦不能直接当成包内资源格式。哈希、尺寸和官方文档交叉证据见 [文档笔记](OFFICIAL_DOCS_NOTES.md)。

### 4.2 源码包与当前 WASM

`[实测]` 用户当前 `demo.mrp` 为 444 字节（SHA-256 `6b62eea8b2ae26d9f0c38822f104ab2f15f1725972e8930e8f1914b07c309926`），其 `start.mr` 是 gzip 包装的 92 字节源码，`mythroad` 是解压后为空的 gzip。当前文件与用户记录的 506 字节绿屏版本不一致；本次只验证实际取得的文件。使用 [五合一工具](../../tools/mrp_tool.py)解包、重打包后字节完全相同，原包和重打包版本分别在 mrpx WASM 成功启动，240×320 画布上均检测到 222 个红色像素；见 [原包证据](../../build/mrp-dialect-evidence/user-demo-evidence.json)及[重打包证据](../../build/mrp-dialect-evidence/user-demo-repacked-evidence.json)。

`[实测]` 自写的 [方言探针](../../tools/mrp_dialect_probe.mr)使用未压缩源码入口、不带 `mythroad` 条目，在同一 WASM 直接启动；31 项中 22 项执行成功、9 项编译错误。按键按下/释放分别投递到 `dealevent`，实得 `(0,18,0)` 和 `(1,18,0)`。各输入、结果、运行环境和复现步骤见 [Lua 差异](LUA5_DIFFS.md#73-wasm-源码方言与事件探针)。这是特定构建、包与输入的运行证明，不是对全部游戏、菜单导入流程或其他模拟器的兼容认证。

`[实测]` 现有扫描器不支持源码入口，上述 gzip 源码包和未压缩源码包均被标为 C；这表示静态工具未能解析，不代表它们不能运行，也不能直接认定 A 类。格式与用户记录的字段差异见 [格式规范](MRP_FORMAT_SPEC.md#15-源码入口包用户样本与-wasm-复测)。

## 5. 插件包

这三个样本不在 664 个应用统计中，位于 [运行时插件目录](../../assets/runtime/mythroad/plugins/)。全部资源均 gzip，EXT 解压后以 `MRPGCMAP` 开头；均没有 `start.mr/start.m`，所以**无入口 Lua 常量可扫描**。

| 包 | 全部资源（解压后字节） | 静态定位 |
|---|---|---|
| `flaengine.mrp` | `skf_plugin.ext` 19,200；`flaengine.ext` 129,312；`logo.bmp` 30,752 | 包名/显示名指向 flash 引擎，具体能力未执行 |
| `netpay.mrp` | `smsend.ext` 8,128；`netpay.ext` 127,064；`smcheck.ext` 22,532；`advsms.ext` 22,116；`smsnet.ext` 14,800；`title_big.bmp` 1,380；`logo_big.bmp` 1,960；`title.bmp` 840；`logo.bmp` 1,000 | 包名/显示名指向短信付费；不执行、不复现 |
| `ose/brwcore.mrp` | `brwcore.ext` 148,224 | 包名/显示名指向浏览器核心，功能细节未执行 |

对这 8 个解压后 EXT 字节做不区分大小写的 ASCII 子串搜索，仅作为标识线索，**不是 Lua 常量表或调用关系**。结果可在 [插件 JSON 档案](../../build/mrp-research/plugins/mrp_catalog.json)的 `native_ascii_markers` 字段复查；如资源未解压，匹配只针对保存的字节：

| EXT | 命中 |
|---|---|
| `skf_plugin.ext` | 无 |
| `flaengine.ext` | `.ext`、`cfunction`、`cfunction.ext`、`netpay`、`flaengine`、`skf_plugin` |
| `smsend.ext` | `.ext`、`netpay` |
| `netpay.ext` | `.ext`、`game.ext`、`netpay` |
| `smcheck.ext` | 无 |
| `advsms.ext` | `netpay` |
| `smsnet.ext` | 无 |
| `brwcore.ext` | `.ext`、`cfunction`、`cfunction.ext`、`netpay` |

663 个已解析的**入口**常量中，`netpay/flaengine/brwcore/skf_plugin` 均未命中；但 `308018_240320_gshddp.mrp` 索引含 `netpay.mrp` 资源。入口常量扫描不覆盖全部次级脚本、字符串拼接、外部配置或实际控制流，不能得出“普通游戏不依赖插件”。

## 6. VM 和运行时关键发现

- 0–34 的 opcode **编号**与 Lua 5.0.3 一致，35–38 追加位运算；语法关键字、`^/#`、数值类型和 `__op` 等存在额外变化。
- GC 都是全量标记清扫；但本 VM 在待 finalizer 的 userdata 引用对象上没有将传播对象的大小加入 `deadmem`，会改变阈值计算。详见 [差异报告](LUA5_DIFFS.md)。
- `0x80` chunk 的源码名是 Proto 字符串；归档加载器原生接受 `0x50..0x80`，但 `>0x50` 不含尺寸自描述。支持此格式不等于任意数字/指针宽度均兼容。
- `_mr_readFile` 常规读取先试外部同名文件再读包；内存包、存在性查询另有分支。`x:/` 切盘是 DSM 工作目录机制，不等于资源路径自动穿透。
- 代码 601 是普通包资源读取；800/801/802 才是原生 EXT 执行链。详细启动、事件和 `plat/platEx/_com/_strCom` 表见 [运行时规范](RUNTIME_SPEC.md)。
- `_and/_or/_xor/_mod/_not` 是注册的 C 函数，其中 `_not` 是逻辑非而非 `OP_BNOT` 的按位非。真实 `MRPPT.mrp` 入口静态字节码中可同时找到 `_mod` 的全局取值/调用和 `BAND` 指令。自写探针已编译并执行四个位指令及五个命名函数桩；没有运行该游戏分支或官方编译器。
- MSVC 19.51.36260 x86 `/O2` 对 VM 表大小表达式产生错误结果；独立最小 C 测试和 `/Od` 对照可复现。研究探针固定 `/Od`，优化构建不能视为已验证。
- full 的 `_strCom(3)` 设置回退信息，`_runFile` 才安排子应用 restart；mini 则以 EXT 路径启动并转发事件，不等于纯 Lua 的缩减版。参数回退与 `%` 路径差异仍待动态验证。
- 当前 WASM 实测支持上述 gzip 与未压缩**源码**入口；31 项独立方言探针和一次按键事件通过采集。归档实现将源码与 `ESC MRP` 二进制分派到不同解析分支，不能把前者称为字节码。

## 7. 待验证问题清单

| # | 问题 | 状态与下一步 |
|---:|---|---|
| 1 | 索引保留字段 | `[实测]` 96,856 项全 0；增加独立来源和非零合成样本，不推断历史全体。 |
| 2 | `headbuf[1]+8` | `[源码引用+实测]` 为索引结束；合成包更改 ±1 后 Python 解析失败。运行时仍需隔离 harness 测试副本。 |
| 3 | chunk 版本 | `[实测]` 662×0x80、1×0x50，1 个无精确入口；扩样覆盖其他版本。 |
| 4 | 命名位函数与 opcode | `[源码引用+自写探针实测]` 仓库 parser 生成并执行四个位指令及命名函数调用，`_not` 非 `BNOT`；官方编译器及游戏执行仍待验证。 |
| 5 | 位图 | `[实测]` 一个包资源符合无头小端 RGB565；源扫雷图为 Windows BMP。其他样本、透明色及异常长度需逐个视觉/调用验证。 |
| 6 | `gb16.uc2` 索引 | `[源码引用+抽样]` Unicode BMP×32；fontmaker 样本是另一套索引。再测试缺字、编码边界及两条渲染路径。 |
| 7 | `*A`、`$` 与盘符 | `[源码引用]` 已识别分支；构造同名外部文件、包内文件及 RAM 包实验确认优先级和所有权。 |
| 8 | `flags` | `[源码引用]` 当前 bit 0 文件名 UTF-8、bit 1 编辑框 UTF-8；历史文档全位定义未找到，其他版本需另查。 |
| 9 | 非 MRPG 魔数 | `[实测]` 本语料无；搜索独立来源，不能外推所有历史变体。 |
| 10 | 包显示名与资源名 | `[源码引用+实测]` 头部显示名与索引资源名独立；畸形编码继续保留原始字节。 |
| 11 | 压缩四态 | `[实测]` 资源 96,629 个 gzip 校验通过、3 个 CRC 异常、224 个不透明/可能 raw，完整 raw-deflate 探测为零。无签名不等于能可靠判定全部为未压缩。 |
| 12 | `_strCom(3)` 子应用/回退 | `[源码引用]` 它只设回退，`_runFile` 安排 restart；实验记录参数在启动、退出、回退中的值。 |
| 13 | 插件依赖比例 | `[实测]` 663 个入口常量未命中四个插件名，但未覆盖次级脚本、动态字符串及外部资源；需扩展静态和运行追踪。 |
| 14 | mini vs full | `[源码引用]` mini 以 EXT 启动并转发事件，`%` 路径和回退入口与 full 不同；构造输入边界验证。 |
| 15 | 头部 CRC | `[实测]` 448/1/9/206 四组；对未知值比较独立生成器和运行时校验，不把未知视为损坏。 |
| 16 | 官方工具和验收样本 | `[待验证]` 在隔离环境编译自写样本、记录工具哈希并运行原包与重打包包；扫雷 C 源码须另行设计纯脚本实现。 |
| 17 | 源码入口扫描 | `[实测]` 当前工具将源码包归 C，不能判定 EXT 依赖；需设计源码解析与依赖规则，再分别验证 gzip/未压缩输入。 |
| 18 | 源码包兼容范围 | `[实测]` 两个源码包在当前 WASM 直接启动；需在菜单导入、其他构建和独立模拟器上验证 `mythroad` 条目、压缩及头字段要求。 |
| 19 | 方言完整语义 | `[实测]` 31 个独立片段及一个按键事件；需补充短路、作用域、异常、数字边界与更多事件，再扩到其他 VM。 |

其他高优先级：验证 32 位优化构建的编译器问题，以及协程 yield/hook、GC finalizer 阈值、0x50/0x80；检查 3 个异常 gzip CRC 的原始来源。任何结论应记录样本 SHA-256、输入、构建宏和输出。

## 8. 工具复现

[mrp_tool.py](../../tools/mrp_tool.py) 不执行字节码或 EXT。所有输出应写入新的空目录；它拒绝覆盖已存在的目标。以下 PowerShell 示例从仓库根执行，语料位置按实际环境设置：

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$Corpus = (Resolve-Path '..\vmrp.github.io-master\public\mrps').Path

# 重建完整 CSV/JSON；输出目录中的目标文件必须不存在。
python tools\mrp_tool.py scan $Corpus --output build\mrp-research-repro
python tools\mrp_tool.py scan assets\runtime\mythroad\plugins --output build\mrp-plugins-repro

# 解包最小 A 样本、打印全部常量、重打包并核对哈希。
python tools\mrp_tool.py unpack (Join-Path $Corpus 'applist.mrp') --output build\applist-unpacked
python tools\mrp_tool.py const build\applist-unpacked\files\000001.bin --output build\applist-constants.json
python tools\mrp_tool.py pack build\applist-unpacked build\applist-repacked.mrp
Get-FileHash (Join-Path $Corpus 'applist.mrp')
Get-FileHash build\applist-repacked.mrp

# 指定实证过的尺寸转换 Tetris 标题图。
python tools\mrp_tool.py unpack (Join-Path $Corpus 'tetris.mrp') --output build\tetris-unpacked
python tools\mrp_tool.py bmp build\tetris-unpacked\files\000012.bin --width 240 --height 120 --byte-order little --output build\tetris-title.png
```

`unpack` 生成编号文件和 `mrp-manifest.json`，保留原始名称、存储流和重复项；`pack` 读取 manifest，也能打包普通目录。已识别的包头 CRC 在修改后重算；未知非零头部值保留并警告，仅明确选择 `--force-builder-crc32` 才覆盖。以原始 DEFLATE 解释无魔数资源时必须明确提供 1 起始 `--raw-index`；gzip CRC 错误仅在 `--allow-bad-gzip-crc` 下恢复并警告。旧 gzip、ZIP 与未识别格式不会假装解压成功。`const` 只接受已解压的 MRP chunk，`bmp` 只针对指定尺寸的无头 RGB565，不做自动格式识别。

文档索引由 [mrp_doc_index.py](../../tools/mrp_doc_index.py) 生成，CHM 来源、复现命令和提取限制见 [文档笔记](OFFICIAL_DOCS_NOTES.md)。当前没有将自动签名匹配当成逐项 ABI 认证。

源码入口与方言的打包、浏览器启动和 JSON/CSV 转换命令见 [Lua 差异](LUA5_DIFFS.md#73-wasm-源码方言与事件探针)；[mrp_dialect_results.py](../../tools/mrp_dialect_results.py)会拒绝不完整日志和现有结果文件。

## 9. 本阶段交付与未完成项

| 交付 | 验证 / 边界 |
|---|---|
| [格式](MRP_FORMAT_SPEC.md)、[运行时](RUNTIME_SPEC.md)、[Lua 差异](LUA5_DIFFS.md)、[文档对照](OFFICIAL_DOCS_NOTES.md) | 每项注明源码、离线实测或待验证；不是所有历史实现的权威完整规范。 |
| [664 包 JSON](../../build/mrp-research-v3/mrp_catalog.json)、[CSV](../../build/mrp-research-v3/mrp_catalog.csv)、[SDK 样本](../../build/mrp-sdk-samples-v2/mrp_catalog.json)、[API 索引](../../build/mrp-doc-inventory-v3/api_index.json) | 本地生成、可重复；扫描分类和源码匹配不证明运行依赖或 ABI。 |
| [五合一工具](../../tools/mrp_tool.py)、[文档索引工具](../../tools/mrp_doc_index.py)及测试 | 离线运行；支持范围和不覆盖已有输出的行为见第 8 节。 |
| [VM 探针](../../tools/mrp_vm_probe.c)、[构建脚本](../../tools/build_mrp_vm_probe.ps1)、[编译器缩减测试](../../tools/mrp_compiler_shift_probe.c) | 本仓库 VM 的自写位运算样本通过 `/Od` 执行；`/O2` 编译器异常已定位，不等同官方编译或游戏验收。 |
| [方言探针](../../tools/mrp_dialect_probe.mr)、[结果转换器](../../tools/mrp_dialect_results.py)、[JSON](../../build/mrp-dialect-evidence/results-final/dialect-results.json)、[CSV](../../build/mrp-dialect-evidence/results-final/dialect-results.csv) | 当前 WASM 实测 31 个片段和一次按键往返；源码包及其原样重打包版本均成功显示。 |
| 官方编译样本、纯 Lua 扫雷、第三方游戏运行结果、完整反编译源码 | **未交付**：尚未在隔离环境执行历史工具/模拟器；扫雷归档为 C 源，不能直接转换；第三方应用以静态接口摘要代替完整源码。 |
