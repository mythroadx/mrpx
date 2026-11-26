# MRP 研究指南

这里集中保存不参与宿主主程序构建的研究源码、历史 SDK 工程和格式样本。它们是研究资料，不是已经标准化、可以直接运行的现代工具链。

## 目录与工程边界

| 目录 | 职责 |
| --- | --- |
| [mrc/](mrc/README.md) | MRP C SDK、打包实现、ABI 偏移及基础库研究 |
| [mrc/asm/](mrc/asm/README.md) | ARM/Thumb 示例源码、原始工程归档、EXT/FMT 与反汇编样本 |
| [mrc/baseLib/](mrc/baseLib/) | 类型大小、基础库函数地址和偏移实验；保留历史 VS 工程 |
| [tests/fixtures/mrp/](../../tests/fixtures/mrp/README.md) | 被桌面/Web 回归实际使用的两个固定 MRP；不属于可清理的编译输出 |
| [mythroad/](mythroad/README.md) | 历史 Guest 运行时、脚本虚拟机、反编译器与 SDK 工程 |
| [include/vmrp/](../../include/vmrp/) 与 [src/](../../src/) | 当前宿主实现和安全边界 |
| [tools/](../../tools/) 与 [scripts/](../../scripts/) | 可用的分析/回归工具及统一构建入口 |

当前运行加载的是 [assets/runtime/cfunction.ext](../../assets/runtime/cfunction.ext)，不是现场编译本目录或 Mythroad 得到的二进制。共享 DSM 接口已独立到 [include/vmrp/dsm.h](../../include/vmrp/dsm.h)，宿主构建不再依赖研究目录；归档中的同名头文件仅转发到此接口。历史源码与现有 Guest 二进制是否完全对应，尚未建立可复现证明。

原根目录 `mrc` 和 `mythroad` 的研究资料迁入此处；实际回归包移到测试夹具目录。Mythroad 原 `build` 中保存的是 SDK 工程源码而非编译产物，现改名为 `sdk`；内部相对目录关系保留。目录迁移没有修改样本字节或像素基线，也没有接入新的 Guest 构建流程。

## 建议的研究顺序

### 1. 先确认 Guest ABI 和宿主映射

- 从 [mr_table_offsets.c](mrc/mr_table_offsets.c)、[mr_c_function_offsets.c](mrc/mr_c_function_offsets.c) 和 [mrc_extChunk_offsets.c](mrc/mrc_extChunk_offsets.c) 入手，对照 [宿主桥接](../../src/bridge.c)中的函数表和状态结构。
- 记录每项的字段宽度、对齐、偏移、调用约定及实际 Guest 输出。Guest 地址是 32 位，不能把宿主原生指针作为 Guest 字段。
- [mrc_base.h](mrc/mrc_base.h) 使用历史 `long` 和指针类型；宿主数据模型可能不同，不能直接将 64 位主机的 `sizeof`/偏移结果当成 ARM ABI。
- 验收要求：形成逐字段对照和可重复实验；任何桥接修改须经过现有 Guest 边界测试与真实 MRP 回归，不能仅凭一次打印修改映射。

### 2. 确认 MRP、EXT 和 FMT 的格式边界

- 从 [mrp.h](mrc/mrp.h)、[mrp.c](mrc/mrp.c)以及 [ARM 样本说明](mrc/asm/README.md)开始，分别研究 MRP 包头/索引和 EXT/ELF 装载数据。
- 使用 [mrp_to_markdown.py](../../tools/mrp_to_markdown.py)检查元信息、索引和问题报告；需要指令分析时使用 [de.c](../../tools/de.c)对应的 `make de` 工具。
- 使用 [mrp_bytecode.py](../../tools/mrp_bytecode.py)提取脚本并逐字节解释前 64 字节；实际扫雷样本的格式与原生扩展依赖见 [ESP32 阶段 0 报告](ESP32_PHASE0.md)。
- ESP32-S3 原生移植的架构、内存、DSM API、EXT 裁剪、字节码、启动流程及验证清单见 [完整可行性研究报告](ESP32_FEASIBILITY.md)；其中明确区分源码事实、设计估算与尚未完成的硬件验证。
- MRP 全量离线扫描、样本、插件和复现命令见 [MRP 研究报告](MRP_RESEARCH_REPORT.md)；分别参照 [格式规范](MRP_FORMAT_SPEC.md)、[运行时规范](RUNTIME_SPEC.md)、[Lua 5.0 差异](LUA5_DIFFS.md)和[历史文档对照](OFFICIAL_DOCS_NOTES.md)。扫描分类和机械提取的 API 索引均是静态线索，不保证运行兼容或 ABI 一致。
- 学习 Mythroad 与 Lua 5.0 的关系、方言、入口格式、事件及可运行示例，先看 [Mythroad Lua 开发指南](MYTHROAD_LUA5_DEVELOPMENT_GUIDE.md)。
- 使用 [mrp_tool.py](../../tools/mrp_tool.py) 的 `scan`、`unpack`、`pack`、`const`、`bmp` 子命令研究索引型包；已识别的包头 CRC 可在修改后重算，未知值保留并警告；旧式 gzip/ZIP 等未支持格式会明确报错，输出不会覆盖现有文件。用 [mrp_doc_index.py](../../tools/mrp_doc_index.py) 索引已解包的 API CHM；完整命令和限制见研究报告。
- [源码方言探针](../../tools/mrp_dialect_probe.mr)可在当前 WASM 中逐项执行；用 [结果转换器](../../tools/mrp_dialect_results.py)将浏览器日志转换为 JSON/CSV。31 项结果、按键事件、环境和复现命令见 [Lua 差异](LUA5_DIFFS.md#73-wasm-源码方言与事件探针)。现有 `scan` 不解析源码入口，分类 C 不等于运行失败。
- [VM 探针](../../tools/mrp_vm_probe.c)及 [构建脚本](../../tools/build_mrp_vm_probe.ps1)可在 MSVC x86 `/Od` 下执行自写位运算样本；`/O2` 的编译器异常、[独立缩减测试](../../tools/mrp_compiler_shift_probe.c)和证据见 [Lua 差异](LUA5_DIFFS.md)第 7 节，不能将探针结果当作游戏或官方工具验收。
- 原有“FMT 去头尾后加 8 字节标记”的结论只基于单一样本，必须保留这个限制。不要据此直接实现通用转换器或改写固定输入。
- 验收要求：多个独立样本均能解释偏移和长度，并能检测截断、越界及不一致数据；明确区分已验证事实和待验证推测。

### 3. 扩展最小 ARM/Thumb 行为实验

- [asm/main.c](mrc/asm/main.c) 和 [asm/asm.s](mrc/asm/asm.s)包含大小端、定点运算和事件显示实验。
- 先复现现有 ARM、Thumb、右软键及触摸场景，再研究互跳、回调、对齐等新行为；同时参考 [Unicorn smoke](../../tests/unicorn_wasm_smoke.c)。
- 验收要求：新实验尽量只验证一个行为，记录输入与预期输出，并在需要兼容的平台运行。新增样本应使用新名称，不能为了通过测试覆盖旧包或替换基线。

### 4. 最后再贯通 Guest 源码构建

- 研究 [Mythroad Makefile](mythroad/Makefile)、[历史 SDK 工程](mythroad/sdk/)和现有二进制的关系，确认编译器、目标架构、生成步骤与许可证。
- 历史 Makefile/VS 工程尚未按当前环境完整验证；不要把它们当作统一构建入口，也不要承诺修改研究源码就会影响页面。
- 验收要求：先在忽略的构建目录生成可复现输出，对比装载布局并运行回归，再决定是否有意替换运行依赖；不直接覆盖当前种子。

## 开始研究的验证步骤

先按[开发环境指南](../DEVELOPMENT.md)准备桌面依赖；以下命令从仓库根目录执行：

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
make test-mrp-runner
make test-mrp
python tools\mrp_to_markdown.py tests\fixtures\mrp --output build\research\mrp-report
```

报告命令不加 `--capture` 时只解析包，不启动模拟器。报告目录必须显式指向 `build`，避免工具默认输出写到固定测试输入附近。WASM 构建和真实浏览器验证沿用开发指南；四个 ARM/Thumb 场景应使用与桌面相同的 Guest 包名。

每次研究记录样本 SHA-256、工具链/平台、具体输入、实际输出和结论适用范围。新的报告、截图、二进制和临时 SDK 数据放到忽略的 `build`，不要把产物写回研究源码目录。已确认的说明、必要源码及有意增加的固定样本再纳入版本管理；历史归档和许可证不应擅自删改。
