# Mythroad 与 Lua 5.0：关系及开发指南

## 1. 结论与适用范围

**Mythroad 的脚本 VM 和语言源自 Lua 5.0，但不是标准 Lua 5.0 的原样实现。**`start.mr` 是应用入口文件名，不是 VM；它可能包含 Mythroad 源码，也可能包含 Mythroad 字节码。`.mrp` 则是装载脚本和资源的应用包。

本指南区分以下证据：

- **[源码]** 本仓库归档的 full Mythroad VM 与 Lua 5.0.3 的对照，详见 [Lua 差异报告](LUA5_DIFFS.md)。归档源码与当前预编译 Guest 是否完全一致，尚未证明。
- **[实测]** 当前 mrpx WASM 中的用户源码包、31 项方言探针及本指南示例；不能推广到所有设备、历史发行版或菜单导入流程。
- **[待验证]** 官方编译器、历史官方模拟器、其他 VM 及本指南未覆盖的语法和 API。

当前 WASM 使用平台自身的 `cfunction.ext` Guest；这与应用包内是否另含 `game.ext` 等原生扩展是两回事。示例不含应用内 EXT，但其运行结果不说明平台可以不依赖自身 Guest。

## 2. 三个层次：包、入口和 VM

```text
应用.mrp：MRPG 容器
  ├─ 其他资源
  └─ start.mr：入口，可存为 gzip 或原始数据
       │
       └─ 读取并按实际存储方式还原 start.mr
             ├─ 源码文本 → Mythroad 源码解析器 → VM 执行
             └─ ESC MRP 字节码 → Mythroad undump → VM 执行
                                              │
                                  绘图、输入等 Mythroad API → 宿主
```

不要把三种开头混为一谈：

| 所在层 | 开头 | 说明 |
|---|---|---|
| 本次研究的 MRP 容器 | `4d 52 50 47`，即 `MRPG` | 应用包，不是 Lua chunk |
| 一种存储形式 | `1f 8b` | gzip 压缩流，解压后才看到源码或字节码 |
| Mythroad 字节码 | `1b 4d 52 50`，即 `ESC MRP` | 本 VM 使用的 chunk 签名 |
| 标准 Lua 5.0 字节码 | `1b 4c 75 61`，即 `ESC Lua` | 与前者不相同 |

`[实测]` 用户提供的 `demo.mrp` 的 `start.mr` 是 gzip 包装的源码；[源码方言探针](../../tools/mrp_dialect_probe.mr)和本指南示例则在当前 WASM 中以**未压缩源码**直接启动。`[源码]` [mrp_load](mythroad/src/mr_api.c)按首字节选择二进制或源码解析路径；没有源码解析器的构建可能报告 `parser not loaded`，见 [mr_noparser.c](mythroad/src/parser/mr_noparser.c)。因此，源码入口能运行是目标运行时能力，不是所有 `.mrp` 的保证。

本次语料中 663 个精确入口可解析为 Mythroad 字节码：662 个版本字节为 `0x80`、1 个为 `0x50`；另有一个包没有精确名 `start.mr/start.m`。这不代表所有历史包都采用这些布局，详见 [格式规范](MRP_FORMAT_SPEC.md)。

## 3. 与标准 Lua 的语言差异

下表的“当前结果”针对仓库解析器和指定测试片段；不能把一次测试外推为完整语言规范。各片段、返回值和错误原文见 [31 项 WASM 方言实验](LUA5_DIFFS.md#73-wasm-源码方言与事件探针)。

| 目的 | 标准 Lua 5.0 常见写法 | 当前 Mythroad 写法与证据 |
|---|---|---|
| 定义函数 | `function f(x) ... end` | `def f(x) ... end`；`[实测]` 前者测试片段编译失败，后者成功 |
| 条件分支 | `elseif` | `elif`；`[实测]` |
| 不等于 | `~=` | `!=`；`[实测]` |
| 逻辑与、或、非 | `and`、`or`、`not` | `&&`、`\|\|`、`!`；符号组合和 `not` 的测试片段 `[实测]`；关键字注册另见 `[源码]` |
| 注释 | `--`、`--[[...]]` | `//`、`/* ... */`；上述测试片段 `[实测]` |
| 数字 | 默认 `lua_Number` 通常为浮点 | 归档默认 `int`；当前 `1.5` 测试片段编译失败 `[实测]` |
| 长度 | Lua 5.0 常用 `table.getn(t)` | 当前 `table.getn(t)` 测试成功；`#t` 测试失败 |
| 位运算 | 标准 Lua 5.0 无对应位运算 opcode | `~`、`&`、`\|`、`^`；四个正常输入的测试片段 `[实测]` |

**尤其注意 `^`：**标准 Lua 5.0 中它表示幂，而当前 Mythroad 测试中 `6 ^ 3` 返回按位异或结果 `5`。`[源码]` 此解析器把二元 `#` 编译为幂指令；[归档 VM 实验](LUA5_DIFFS.md#74-归档-vm-的幂运算钩子)表明数值运算依赖全局 `__op`，不能机械地把所有 `^` 替换为 `#` 后宣称语义等价。混用算术和位运算时加括号，优先级及幂的实际目标环境行为应单独测试。

可沿用部分 Lua 思路：`local`、表、数值 `for`、`while`、`repeat`、字符串拼接 `..` 在本次指定片段中均成功；更复杂的闭包、协程、库函数及错误边界仍需按目标构建测试。`[源码]` 此 VM 和 Lua 一样仅将 `nil` 与 `false` 视为假；`0` 不是假，不能套用 C 的条件判断习惯。

**不要对整个源码文件做简单字符串替换。**关键字可能出现在字符串、注释或标识符里；迁移标准 Lua 源码时应做词法级转换，并用目标 VM 分别验证语法和语义。

## 4. 数值、字节码和 C 接口

`[源码]` [mr_user_number.h](mythroad/src/h/mr_user_number.h)默认定义 `USE_INT`；当前研究的目标配置使用 32 位整数。整数除法、溢出、负数及除零边界不能照搬默认浮点 Lua，也不能假设所有设备采用同一数值配置。游戏中的比例和坐标计算应明确整数缩放，并对边界编写目标运行时测试。

`[源码]` [mr_undump.h](mythroad/src/h/mr_undump.h)定义 `ESC MRP`，版本检查接受 `0x50..0x80`；`0x80` **不是 Lua 5.1 的版本标识**。`0x50` 头包含宽度和数值测试，`>0x50` 头不提供同样的宽度自描述。版本检查通过仍不证明整数宽度、布局或指令语义兼容。

`[源码+实测]` 标准 Lua 5.0 的 opcode 编号 `0..34` 在归档 VM 中保持相同编号，Mythroad 追加 `BNOT=35`、`BAND=36`、`BOR=37`、`BXOR=38`。编号相同不等于语义完全相同。命名函数 `_and/_or/_xor/_mod/_not` 是另一层 API，其中 `_not` 是逻辑非，不是位取反。详见 [指令和实验](LUA5_DIFFS.md#1-指令集)。

**不要只改文件魔数、套用标准 `luac`，或把标准 Lua VM 直接替换 Mythroad VM。**签名、头布局、数值模型、扩展指令、语言和平台库都需要匹配。C 层名称如 `lua_State → mrp_State`、`lua_load → mrp_load` 有同源关系，但不保证 ABI 可互换；编译宏、指针宽度和 `mrp_Number` 均需核对。

## 5. 编写应用时的 API 与事件

标准 Lua 库和 Mythroad 平台 API 是不同层。以下名称来自 [full 运行时注册表](mythroad/mythroad.c)，不保证所有 Guest/宿主均实现同样能力：

| 接口 | 用途 | 本指南证据 |
|---|---|---|
| `_clearScr(r,g,b)` | 清屏 | `[实测]` 示例使用 |
| `_drawText(text,x,y,r,g,b)` | 绘制文本 | `[实测]` 示例使用 ASCII 文本 |
| `_dispUp(x,y,w,h)` | 更新指定屏幕区域 | `[实测]` 示例使用 240×320 |
| `dealevent(code,p0,p1)` | 接收输入等事件的**全局函数** | `[实测]` 两次按键；`[源码]` full 运行时按全局名查找 |
| `_timerStart(0,ms,"callback")` | 指定到期后调用的全局函数名 | `[源码]` 当前归档的第 2 参数转为 `uint16`，第 3 参数为名称；本指南示例未做定时器实测 |

`[实测]` 当前 WASM 中按键按下的 `code=0`、释放的 `code=1`；`p0` 是按键码，已测右软键为 18、数字 1 为 1。此次示例只对按下计数，释放不计数。其他事件和宿主参数见 [运行时事件表](RUNTIME_SPEC.md#22-mr_event-内层消息)。

`[源码]` full 运行时在定时器到期时按名称查找 Lua **全局函数**，并先将当前定时器状态置空；需要周期行为时应在目标运行时验证重新启动及暂停/恢复。不要把示例代码中的事件处理或猜测的 `onKey`、`timerStart` 名称当作通用契约。不同编译配置下可用的 `string`、`table`、`file`、网络和 GUI 能力也可能不同。

中文显示需要另行验证：当前示例只用 ASCII。归档 `_drawText` 的非 Unicode 路径使用编码转换；输入字节编码、字库和可选参数都可能影响结果，UTF-8 源文件并不自动证明中文正确显示。先验证编码转换及字库，再引入中文文本。

## 6. 可直接运行的完整示例

完整文件为 [mrp_lua_guide_example.mr](../../tools/mrp_lua_guide_example.mr)，内容如下；以下是**一个完整入口文件**，不是待拼接片段：

```lua
local presses = 0

def redraw()
  _clearScr(0, 0, 0)
  _drawText("Mythroad Lua guide", 8, 8, 255, 255, 255)
  _drawText("Presses: " .. _str(presses), 8, 32, 255, 255, 0)
  _dispUp(0, 0, 240, 320)
end

def dealevent(code, p0, p1)
  if code == 0 then
    presses = presses + 1
    print("GUIDE|press|" .. _str(p0) .. "|" .. _str(presses))
    redraw()
  end
  return 0
end

redraw()
print("GUIDE|ready")
```

`redraw` 和 `dealevent` 是全局函数；`presses` 是二者共享的局部上值。画布宽高在此示例中固定为 240×320，不是所有设备的通用尺寸。

### 6.1 生成包并运行

先准备仓库现有的 `build\web` WASM 构建，参见 [开发环境指南](../DEVELOPMENT.md#4-wasm-最小复现)。以下 PowerShell 命令从仓库根执行；如输出目录已存在，换一个新目录，同时修改下方 URL：

```powershell
$root = 'build\mythroad-guide-demo'
if (Test-Path -LiteralPath $root) { throw 'Output already exists; choose a new directory' }
$src = Join-Path $root 'input'
New-Item -ItemType Directory -Path $src -Force | Out-Null
Copy-Item -LiteralPath 'tools\mrp_lua_guide_example.mr' -Destination (Join-Path $src 'start.mr')
python tools\mrp_tool.py pack $src (Join-Path $root 'example.mrp')
if ($LASTEXITCODE -ne 0) { throw 'Pack failed' }
```

普通目录模式的 [mrp_tool.py](../../tools/mrp_tool.py)按原样存储文件，不自动 gzip，也不添加 `mythroad` 标记。直接启动这一布局在当前 WASM 中通过；**其他运行时或菜单导入是否接受待验证**。用户提供的另一份 `pack_mrp.py` 默认添加空 gzip 标记并压缩源码；其固定索引、覆盖输出及校验限制见 [格式规范](MRP_FORMAT_SPEC.md#15-源码入口包用户样本与-wasm-复测)。

从仓库根启动仅监听本机的服务：

```powershell
python -m http.server 18791 --bind 127.0.0.1 --directory build
```

打开 `http://127.0.0.1:18791/web/main.html?f=../mythroad-guide-demo/example.mrp&name=example.mrp`。页面应显示标题及 `Presses: 0`；点击右软键，再点击数字 1，计数应依次增加。控制台应出现：

```text
GUIDE|ready
GUIDE|press|18|1
GUIDE|press|1|2
```

完成后在服务器终端按 Ctrl+C。服务会向本机其他进程暴露 `build` 目录，不用于公网；不要把敏感文件放在被服务的目录中。

### 6.2 已验证范围

`[实测]` 用本仓库工具打包后再解包，入口与示例源码字节一致。当前 WASM 成功启动，画布为 240×320；按下右软键后画布像素发生变化，两次按键及释放后的日志与上面一致。还用页面实际按键重复了相同操作。

此次示例源码 SHA-256 `c695d20e4b73b694fb5c79a3e36c356de2f83dcea78b251b7fce6f080e0809bf`，包 SHA-256 `4ec31d43caa68df93e4ebc7e7934522d06056a4b566985e75eb7a3235713468c`；`mrpx.wasm` 为 `caca4a0b84be07210b9daad6678365fbb868f7e9700416c1180ffeaaf3674960`，平台 Guest 为 `8f85e3cf8f0ed4a8e09eb658f9daba566989fbc06c510e4e76cd474dd275cad5`。本机浏览器日志位于被忽略的 `build\mythroad-lua-guide-validation\browser-console.log`，不随仓库发布；结果只适用于该构建，源码换行或工具链变化可能改变哈希。

## 7. 检查已有包与常见误区

1. 用 [mrp_tool.py](../../tools/mrp_tool.py) 的 `unpack` 在新目录解包，并通过 `mrp-manifest.json` 找到入口的编号文件；不要假定它永远是第一项。文件扩展名不能区分源码和字节码。
2. 如果还原内容以 `ESC MRP` 开头，可对该**已解压字节码文件**使用 `mrp_tool.py const`。它不运行字节码；常量或静态引用不等于实际调用。
3. 如果还原内容是源码，应按源码和目标 VM 分析。当前 `scan` 不解析源码入口，会归为 C；这表示工具不支持，不等于应用不能运行，也不能自动判成无 EXT 的 A 类。
4. 用户提供的旧实验 `mrp_inspect.py` 中，`pack` 是占位实现，且把 `ESC MRP` 当作容器开头；其将 `0x80` 标为 Lua 5.1 的说明也不适用。对该脚本的 ASCII 正则匹配结果不能当作完整 Proto 常量表。

| 现象 | 优先检查 |
|---|---|
| `parser not loaded` | 目标构建是否包含源码解析器；源码和字节码不可互相假冒 |
| `function`、`--` 或 `~=` 附近编译错误 | 当前方言写法；不要凭标准 Lua 的语法推断 |
| `err:1005` 或头尺寸错误 | 是否拿错容器/压缩流、chunk 签名、版本及目标宽度；不要只改魔数 |
| 控制台出现 `dealevent is nil!` | 是否定义了目标运行时所查找的全局 `dealevent` |
| 已绘制但画面不变 | 确认目标屏幕尺寸、绘图坐标和 `_dispUp`；再查看宿主日志 |
| 中文乱码 | 分别核对源字节编码、文本 API 标志、编码表和字库 |

## 8. 建议的后续开发顺序

1. **固定目标环境**：记录 mrpx/Guest 哈希、入口形式、包哈希和运行日志，先复现本指南示例。
2. **扩展语言测试**：逐项验证作用域、短路、整数边界、协程、异常和标准库；每次只改变少数输入，不把静态推断记作实测。
3. **完善平台能力**：按绘图、按键、定时器、资源、存档顺序各写独立可运行样本，再考虑音频和网络；对缺失 API 明确记录。
4. **构建真实应用**：从无应用内 EXT 的小程序开始，保留源码、包、依赖清单及可重现的操作记录；静态分类不是运行兼容认证。
5. **交叉验证**：在可控环境测试官方编译器、历史模拟器及目标设备，分别确认源码/字节码、压缩、入口和回调差异；不得以当前 WASM 的成功替代这些验证。

深入材料：[格式规范](MRP_FORMAT_SPEC.md)、[运行时规范](RUNTIME_SPEC.md)、[Lua 5.0 逐项差异](LUA5_DIFFS.md)、[研究报告](MRP_RESEARCH_REPORT.md)。

## 9. Windows 原生开发原型

此原型直接编译归档 VM 和源码解析器，在 Windows x86 进程中运行**新应用源码**，不使用 Unicorn，也不加载平台或应用内 ARM EXT。它是第一阶段的最小开发闭环，不是完整 Mythroad 平台，也不保证与第 6 节的 WASM Guest 像素或行为一致。

需要 Visual Studio 的 x86 C 编译工具和 Windows SDK。工作区根目录运行：

```powershell
.\tools\build_mythroad_native.ps1 -OutputDirectory build\mythroad-native-draw
.\build\mythroad-native-draw\mythroad-native.exe tools\mrp_lua_guide_example.mr
```

构建使用 x86、`/Od`；目标目录必须不存在，避免覆盖现有文件。找不到 Visual Studio 时可以显式传 `-VcVarsAll`。当前 VM 依赖 32 位指针和整数布局，不能将此构建直接改成 x64；归档源码的优化级别边界见 [MSVC 优化实验](LUA5_DIFFS.md#72-msvc-优化构建异常)。关闭窗口退出，控制台显示 `print` 输出和错误。

较完整的贪吃蛇示例：

```powershell
.\build\mythroad-native-draw\mythroad-native.exe examples\mythroad-native\snake.mr
```

进入后按 Enter 或 F1 开始/暂停，方向键控制移动，F2 重新开始（重新开始后仍需按 Enter 或 F1）。游戏包含计分、进程内最高分、吃食物增长、逐级加速、碰撞和满格胜利。食物按移动步数确定位置，并跳过被蛇占用的格子；这不是随机数。画面使用矩形、线和点绘制，文字字体效果取决于系统；LSK 和 RSK 分别表示左、右软键。

将示例作为 `start.mr` 打包后，也可由本仓库 Windows Unicorn 模拟器加载。实际验证中，源码包成功启动并显示棋盘；测试专用包装器通过模拟右软键启动游戏并记录定时器移动、吃食物、转向、暂停/恢复和撞墙。不等于对其他 Guest 或设备的兼容保证。模拟器的实际键盘映射与原生原型不同：Enter 为确认键，方向键或 WASD 移动，Q（或左方括号）是左软键，E（或右方括号）是右软键；F1 控制模拟器面板，不是游戏左软键。

原型接受单个不超过 1 MiB 的源码文件，不读取 MRP 包、字节码或 EXT，也不提供应用资源和存档。它没有注册完整基础库、`table`/`string` 库、图片、音频、网络、短信或支付 API；不要把缺失接口当作成功。源码应为 UTF-8 无 BOM，应用应只调用下列已实现接口：

| 接口 | 原型行为 |
|---|---|
| `print(...)` | 输出字符串和整数；其他类型输出类型名 |
| `_str(value)` | 将整数或字符串转为字符串 |
| `_clearScr(r,g,b)` | 清屏，颜色分量为 0–255 |
| `_drawRect(x,y,w,h,r,g,b)` | 绘制填充矩形，裁剪到画布；宽或高不为正时不绘制 |
| `_drawLine(x1,y1,x2,y2,r,g,b)` | 绘制包含两个端点的直线，屏幕外像素被裁剪 |
| `_drawPoint(x,y,r,g,b)` | 绘制一个点，屏幕外的点不绘制 |
| `_drawText(text,x,y,r,g,b)` | 用 Windows GDI 绘制 UTF-8 文字；不支持额外的历史字体/编码参数 |
| `_dispUp(x,y,w,h)` | 请求重绘整个窗口；暂不使用矩形优化 |
| `_timerStart(0,ms,"callback")` | 启动一个一次性定时器（1–65535 ms）；到期调用同名全局函数，重新启动会替换上一个 |
| `_exit()` | 结束运行 |
| `dealevent(code,p0,p1)` | 可选的全局事件函数；接收按键和鼠标按下/释放 |

绘图坐标和尺寸须在有符号 16 位范围内，颜色分量须在 0–255 范围内；原型对无效参数报错，而不模仿历史实现的截断。窗口客户区固定为 240×320，使用 Windows 字体和 32 位画布，不模拟原来的 RGB565 颜色或字体栅格。数字键、方向键、Enter、F1/F2、A/B 分别映射到对应 Mythroad 键；鼠标左键传递坐标。GDI 字体取决于系统，中文接受 UTF-8 但不能保证所有字形或历史字体效果。`--check` 运行入口后退出，`--check --event 0 18 0` 可额外注入一次右软键按下事件：

```powershell
.\build\mythroad-native-draw\mythroad-native.exe --check --event 0 18 0 tools\mrp_lua_guide_example.mr
.\tests\mythroad_native_tests.ps1 -Executable build\mythroad-native-draw\mythroad-native.exe
.\tools\build_mythroad_native.ps1 -OutputDirectory build\mythroad-native-graphics-check -GraphicsTests
.\build\mythroad-native-graphics-check\mythroad-native-graphics-tests.exe examples\mythroad-native\snake.mr
```

测试覆盖示例启动及注入事件、贪吃蛇逻辑和定时器、绘图接口与游戏画布的部分像素、UTF-8 检查，以及源码、接口、事件和 EXT 错误；它不是完整兼容或全画面像素测试。源码以本机进程权限执行，VM 没有安全沙箱，不应运行不可信代码。原生重建和后续分发仍需核查归档各部分的来源和授权。
