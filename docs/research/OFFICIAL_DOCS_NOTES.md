# Mythroad 文档、SDK 与归档源码对照

## 0. 来源、证据和限制

本文中的 `[官方文档]` 指社区归档中带原厂署名的历史手册，并非已独立认证的原版或许可证；`[归档文献]` 指作者及完整性未独立确认的资料。`[实测]` 为离线解析和字节比较，`[源码引用]` 为对当前归档实现的阅读，`[待验证]` 不代表已运行。

资料固定到 [vmrp/mrpdev 的 `9ee39b0bb4c6454b9abb9b456379e5ae37312967`](https://github.com/vmrp/mrpdev/tree/9ee39b0bb4c6454b9abb9b456379e5ae37312967) 和 [vmrp/MythroadSDK 的 `2df8647d674b8da1f8fff91d6c84f177efb5ae16`](https://github.com/vmrp/MythroadSDK/tree/2df8647d674b8da1f8fff91d6c84f177efb5ae16)。以下 SHA-256 对应本次取得的文件；匹配 Git blob 仅证明其与上述提交一致，不证明原始来源或授权状态。

| 归档文件 | SHA-256 |
|---|---|
| `SKYENGINE API.chm` | `88f1bc8d8c8ba7c94e25cfaa8abe925c89769011ea7712140d47dc9be58dc7d7` |
| `Mythroad API V1.05.chm` | `e80226e4542707657b87f26976f6c39ecdc4a98450452a4c2944cac6b563c699` |
| 开发指南卷一 | `6d3ad24b3ae56babe294d0062fcdad70375fb38156f201f86d735559bfd7577f` |
| 开发指南卷二 | `1a38f2b0f18580c2faf2acf64f9599647f8726da9ff6dc0e0eff426abbd08be0` |
| 开发指南卷三 | `2c80f7ede8617a107cb80b43f1991682818deb144a1642ede8441de1111fc79a` |
| `1winmine(正式版).zip` | `957e1afa15de8ee4d9ba0a4ba1896cae7a7984d1e745f2db9d3db0766b7aa271` |
| `sky_fontmaker_v2.0.0616.rar` | `e368f174233825a380845a5ba8f856a06679f6220555ba8c069fbd6b380fd88b` |
| `MRP插件浅析.rar` | `c49d835df2f1176d6af23c9c156efdc06cc373f2950274a31654eb23c5c5e8ab` |
| `整理的ext重要函数.c` | `2643f3a4d1908020096258acde4de0eba6aa87b19d4c402dc3896f86f3333856` |

CHM 已离线解包为 HTML；本次没有安装或运行归档可执行文件、模拟器、MSI、EXT，亦没有执行收费逻辑。归档中的技术内幕主要介绍 C SDK 和原生插件；不能将其示例中的原生模块结构套用为本次语料中所有入口的分类结论。

## 1. API 索引与覆盖范围

从两个 API CHM 的**顶层** HTML 主题机械提取候选签名，以同名文本和 `dsm.c` 的 `switch` 做定位：

- [API JSON 索引](../../build/mrp-doc-inventory-v3/api_index.json)和 [CSV 索引](../../build/mrp-doc-inventory-v3/api_index.csv)：459 个主题，其中 SKYENGINE `mr_*` 273、Mythroad `mrc_*` 186；重复的平台 code 主题分别计数。
- 451 个主题提取到候选声明，含一个函数指针声明。余下 8 个主题的定义与主题名不完全一致：`mrc_getUptimer` 页写 `mrc_getUptime`；`mrc_pause` 页写 `mrc_appPause`；`mrc_Socket_SendNoSdkBuf` 页写 `mrc_Socket_Send`；`mrc_tileLoad` 页写 `mrc_tileLoadEx`；`mrc_memcpy/memmove/strlen/strncpy` 四页的定义分别写不带 `mrc_` 的 C 库名。不能自动认定这些是别名、笔误或同一 ABI，故保留为空待人工核对。
- 197 个 `mr_plat/mr_platEx` code 主题中，19 个数值对应当前 DSM 的显式 case、157 个走默认分支、21 个范围主题未比较。显式 case 也可能只是返回 `MR_IGNORE` 的占位；默认分支只表示**此 DSM 源码**没有相应 case。
- 267 个主题有 `dsm.c`/`mythroad.c` 同名函数定义文本；这不是 267 个唯一函数，也不证明签名、条件编译、返回值或完整 ABI 一致。很多 `mrc_*` 属于另一层，未匹配不等于运行时不存在。

索引不是逐条语义认证，更不是官方 API 全部版本的完备表。现有 DSM 分支及返回值见 [运行时规范](RUNTIME_SPEC.md)；未覆盖的官方参数、返回值和范围 code 仍需人工核验。

复现时先用合适的 CHM 解包工具准备两个 HTML 目录，再在仓库根执行：

```powershell
python tools\mrp_doc_index.py 'C:\path\skyengine' 'C:\path\mythroad-api' `
  --source docs\research\mythroad --output build\mrp-doc-inventory-repro
```

输出目标必须不存在。`signature_source`、`exact_source_definitions`、`dsm_dispatch` 表示提取方式及定位线索，不表示语义已认证。

## 2. 已人工核对的契约和差异

| 项 | 文档与源码/实测 | 状态 |
|---|---|---|
| 事件与按键 | `[官方文档]` `mr_event.htm` 描述同步投递及事件值 0–18，另列 `MR_MCHAT_EVENT=100`；`[源码引用]` 当前头文件未定义 100，但 full `mr_event` 不按事件值做白名单 switch。具体回调和按键值见 [运行时规范](RUNTIME_SPEC.md)。 | 不因未定义枚举就断言数值 100 不能转发；实际应用处理待测。 |
| 事件返回值 | `[官方文档]` `mr_event.htm` 提醒平台返回值不能当作应用 `mrc_event` 返回值。`[源码引用]` full 的原生 hook 若返回非 `MR_IGNORE` 则直接转发该值；Lua `dealevent` 以零返回值调用并最终返回 `MR_SUCCESS`。 | 原生 hook 不必等同文档中的应用回调，完整链路待动态验证。 |
| 工作盘符 | `[官方文档]` `mr_platEx(1204).htm` 说明 A/B/C/D、X/Y/Z；`[源码引用]` 此 DSM 实现 A/B/C/X/Y/Z，但无 D 分支，返回 `MR_IGNORE`。 | 文档列出 D 同时又笼统写 D–W 保留，存在内部措辞重叠；不推断其他实现。 |
| 存档 | `[官方文档]` 卷二 `_游戏存档_管理.htm` 建议以包头短名建立存档目录，并从包信息读取短名；文档还介绍可选的终端绑定方案。 | 只记录目录约定，不复现终端绑定或收费。 |
| 位图内存 | `[官方文档]` `mr_drawBitmap.htm` 描述内存像素为行优先、左到右、上到下的 16 位 RGB565。`[实测]` 一个 Tetris 包资源与无头小端 RGB565 相符。 | 函数参数布局不是所有磁盘图片格式的保证；源 BMP 示例见第 4 节。 |
| 字符点阵 | `[官方文档]` `mr_getCharBitmap.htm` 的默认契约为连续紧凑位排列。`[源码引用]` 当前 DSM 对 ASCII 返回宽 8、高 16，但仍返回每行两字节的 32 字节槽；平台绘字与通用绘字的条件分支不同。 | 见第 3 节；这是可能的构建配置不匹配，不宣称已在运行时复现。 |
| 随机数 code | `[官方文档]` `mr_plat(1211).htm` 文件名为 1211，但签名写 `code=1221`；页面参数/返回描述也有歧义。`[源码引用]` 此 DSM 的随机数 case 为 1211。 | 文档内部冲突，记录双方；实际设备和其他版本待测。 |
| `flags` | `[源码引用]` 当前 [dsm.h](../../include/vmrp/dsm.h) 的 bit 0/1 分别控制文件系统和编辑框 UTF-8 转换。 | 尚未在已核对的官方主题中找到其完整位定义；其他版本待查。 |

`mr_open/read/write/seek` 等接口的名称、候选签名和源码定位在 API 索引；文件数据的偏移、压缩、包头等格式以 [格式规范](MRP_FORMAT_SPEC.md) 中对应的源码及样本为依据，而不是由 API 名称推断。卷三介绍 SDK 的打包与加载过程，但本次尚未取得能逐字段背书 `MRPG` 二进制偏移及 Lua dump 的官方逐字段表。

## 3. 两种不同的字库

### 3.1 系统 `gb16.uc2`

`[源码引用+实测]` 本仓库 [gb16.uc2](../../assets/runtime/mythroad/system/gb16.uc2) 为 65,536 × 32 字节；[dsm.c](mythroad/dsm.c) 以 Unicode BMP 码点乘 32 定位，每行两字节、高位在左。已按此方式观察数字及中文样本，不等同于用 GBK 区位号直接定位。

针对 `MR_PLAT_DRAWTEXT` 平台绘字与不启用该宏、启用 `MTK_MOD` 的通用绘字分支，Python 按各自源码位序及读取方式静态模拟：U+0030、U+0041 分别有 28、32 个像素差异（比较 ASCII 前 8 列），U+4E2D 有 74 个像素差异。**这不是运行时截图或设备实测**；需要用同一字库、同一帧缓冲及明确宏组合运行两个分支，逐像素比较并检查 ASCII 行跨度及位序。

### 3.2 fontmaker 归档中的另一种资源格式

`[归档文献+实测]` `sky_fontmaker_v2.0.0616.rar` 内含单独的 `fontRead` 源码和 `font.mrp`；不运行安装包。该样本 SHA-256 为 `7ce5a9fb58cbda2a28868b52cddae9ccc81485aabdcf8eed6e9a3df6c3d0858c`，10 项资源均成功 gzip 解压，原始重建逐字节一致。样本资源清单在 [独立样本档案](../../build/mrp-font-sample/mrp_catalog.json)。

其 `fontRead.c` 将 `fun_N.bin` 和 `fgb_N.bin` 分别作为 Unicode 和 GB2312 索引，`fpoint_N.bin` 作为点阵数据。源码的实际查找为**每项 5 字节：2 字节大端字符码 + 3 字节大端点阵偏移**；同文件的“16 位偏移”注释与实际读取三字节不一致。每个 `fun_N.bin` 实测为 1,040 字节、208 项，按字符码升序，所有偏移指向对应点阵文件内。

`fpoint_N.bin` 首字节是字体高度；索引指向的记录以宽度、点阵字节数两个单字节开头，之后是高位优先的紧凑点阵。三个 Unicode 索引中各 208 项的记录长度都满足 `ceil(width * height / 8)` 且未越界。例如 U+4E2D 的三个索引分别为 `4e 2d 00 0c 9e`、`4e 2d 00 06 59`、`4e 2d 00 03 fc`，对应偏移 3,230、1,625、1,020。

这套 **有独立索引、可变字宽** 的样本格式与系统 `gb16.uc2` 的定长 `codepoint*32` 格式不同；不能用前者证明后者的文件布局，也不能只凭文件名推广到所有 fontmaker 版本。未自产字库、未运行字体生成器或样本程序。

## 4. SDK 包、扫雷源码和位图

`[实测]` SDK `samples` 下 19 个 `.mrp`：均为索引型 MRPG，19 个原始重建字节相同、19 个头部匹配归档 builder 的整文件 CRC；按静态入口规则 A=1、B=18。清单见 [SDK 样本 JSON](../../build/mrp-sdk-samples-v2/mrp_catalog.json)和 [CSV](../../build/mrp-sdk-samples-v2/mrp_catalog.csv)。A 不是运行兼容认证。

唯一 A 样本 `hello/helloworld.mrp` 为 527 字节，SHA-256 `70e6a34593338d45b1d43e7ebb1647477666202a2965fe8b5e92e0c6eb4143d1`，含 `mythroad` 与 `start.mr`；入口的静态全局取值包括 `_clearScr/_drawText/_dispUp`。已执行解包→打包→再解包和逐字节比较；同目录现有源码包含更多静态符号，不能证明该包由当前源码或本次编译产生，更不能据此声称已经运行。

`[实测]` `1winmine(正式版).zip` 含 `Winmine.c` 和四张 BMP，没有 Lua 源；其中 `mrc_appEvent` 等属于 C SDK。删除少量 EXT 调用不能直接变成纯 Lua 扫雷；要交付纯脚本版本须另行设计和实现，并取得相应使用权限。本次没有产出可运行的无 EXT 扫雷。

四张**源码输入图**均为带 54 字节头、正高度、24 位 Windows BMP：`block` 16×256、`face` 24×120、`focus` 22×22、`num` 13×276。BMP 每行补齐至 4 字节；这些输入不能证明 builder 打包后的图像格式。`[官方文档]` `mr_drawBitmap` 的 RGB565 内存契约与这四张源图格式不冲突。

归档 `mrpbuilder.NET.exe` 是未签名 32 位 PE，SHA-256 `6e81fdfb5344f5a948d17ab4b2170513ddd0f845640b8cb42203bd10cd04c2be`。当前并非专用隔离环境，未运行它；**官方**编译、官方工具位运算实验、模拟器加载及真机验证均未完成。仓库 parser 的自写位运算探针及编译器限制见 [Lua 差异报告](LUA5_DIFFS.md)第 7 节，不能将其当成官方工具验证。

## 5. 插件资料与边界

`[官方文档]` 卷三“插件概述/插件开发”说明一种插件由头文件声明、链接桩和独立 MRP 实现组成，示例通过消息向对应 EXT 分发；这是 C SDK 插件设计说明，不证明每个插件或 Lua 包使用相同路径。

`[归档文献]` `MRP插件浅析.rar` 包含 PowerPoint。只离线解析其可辨识文本记录；它将内置/外置插件、主模块、插件标识和事件转发作为主题，文本流并非完全解析。`整理的ext重要函数.c` 是社区分析稿，其中的函数表和结构体需按具体 SDK 与编译宏核验，不能当作所有设备的 ABI 规范。本研究没有加载或执行其中的 ARM 代码。

SDK 的 `mr_helper.lib` 与字体静态库保留 ARM ELF 符号；其中部分代码片段已与 SDK 样本及用户语料作有范围限制的字节对照。`Small_Font.rar` 中还找到与用户语料完整哈希相同的 `skyfont.ext` 和 `verdload.ext`；结果与未验证边界见 [EXT 研究报告](EXT_ANALYSIS.md#7-历史-sdk-中的符号化目标文件及语料对照)。

三个插件包的实际资源、ASCII 标识扫描及局限见 [研究报告](MRP_RESEARCH_REPORT.md)。663 个解析成功的入口没有命中所选插件名，但未覆盖动态字符串、其他脚本及外部文件；普通游戏的真实依赖比例仍未知。

## 6. 后续验证

1. 人工复核索引中的 8 个主题名与声明不一致项、全部平台 code 的参数/返回值以及范围主题；区分存在 case 和真正实现。
2. 在隔离环境重建有合法来源的工具链，编译含命名位函数和语法运算符的自写脚本，记录编译器哈希、命令、输出及 VM 执行结果。
3. 对 `mr_event`、盘符 D、子应用回退与两套字体绘制分支构造单一变量测试；记录编译宏及逐像素/回调结果。
4. 确认 fontmaker 样本的生成来源与授权，再使用独立样本验证索引、缺字和非 ASCII 字符；不要将其格式与 `gb16.uc2` 混同。
5. 确认格式写入器的重打包结果能被独立实现加载后，才讨论可运行的验收样本；没有运行测试前不把离线往返称为兼容性证明。
