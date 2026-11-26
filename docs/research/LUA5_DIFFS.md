# Mythroad VM 与 Lua 5.0.3 的差异

## 0. 对照范围

本仓库 [mythroad/src](mythroad/src) 与官方 [Lua 5.0.3](https://www.lua.org/ftp/lua-5.0.3.tar.gz)（下载副本 SHA-256 `1193a61b0e08acaa6eee0eecf29709179ee49c71baebc59b682a25c3b5a45671`）对照。官方版本是 5.0.3，不代表证明了与所有 5.0.x 子版本的差异。对 16 个核心 `.c` 文件做了去注释、去 include 和常见前缀归一化后的 token 频次粗比，并人工查看字节码、算术、词法、协程、GC、库；这不是形式化等价证明，也不代表逐行穷尽了每一处平台改动。

**重要修正：**opcode 编号只追加四条，并不等于语言语法、数值、元方法和 GC 等行为除这四条外完全相同。

## 1. 指令集

定义及执行分别见 [mr_opcodes.h](mythroad/src/h/mr_opcodes.h)、[mr_opcodes.c](mythroad/src/mr_opcodes.c)、[mr_vm.c](mythroad/src/mr_vm.c)，与官方 `lopcodes.h` 的 enum 按顺序逐项对照。标准编号 0–34 未发生增删或位移；下表不意味着所有执行语义相同。

| 标准值 | Mythroad 值 | 指令 | 标准值 | Mythroad 值 | 指令 |
|---:|---:|---|---:|---:|---|
| 0 | 0 | MOVE | 1 | 1 | LOADK |
| 2 | 2 | LOADBOOL | 3 | 3 | LOADNIL |
| 4 | 4 | GETUPVAL | 5 | 5 | GETGLOBAL |
| 6 | 6 | GETTABLE | 7 | 7 | SETGLOBAL |
| 8 | 8 | SETUPVAL | 9 | 9 | SETTABLE |
| 10 | 10 | NEWTABLE | 11 | 11 | SELF |
| 12 | 12 | ADD | 13 | 13 | SUB |
| 14 | 14 | MUL | 15 | 15 | DIV |
| 16 | 16 | POW | 17 | 17 | UNM |
| 18 | 18 | NOT | 19 | 19 | CONCAT |
| 20 | 20 | JMP | 21 | 21 | EQ |
| 22 | 22 | LT | 23 | 23 | LE |
| 24 | 24 | TEST | 25 | 25 | CALL |
| 26 | 26 | TAILCALL | 27 | 27 | RETURN |
| 28 | 28 | FORLOOP | 29 | 29 | TFORLOOP |
| 30 | 30 | TFORPREP | 31 | 31 | SETLIST |
| 32 | 32 | SETLISTO | 33 | 33 | CLOSE |
| 34 | 34 | CLOSURE | — | 35 | BNOT |
| — | 36 | BAND | — | 37 | BOR |
| — | 38 | BXOR | | | |

指令为 32 位：`opcode=bits[0:5]`，`C=bits[6:14]`，`B=bits[15:23]`，`A=bits[24:31]`；`Bx` 占 B/C 共 18 位，`sBx` 有偏移。`RK(x)` 小于 `MAXSTACK` 读寄存器，否则读常量 `x-MAXSTACK`。这里是 Lua **5.0** 的字段顺序，不能直接套用其他 Lua 版本布局。

| 新指令 | 格式 | 已见执行行为 |
|---|---|---|
| BNOT | A B | 数值时 `R[A]=~(long)RK(B)`；校验表规定 B 为寄存器 |
| BAND | A B C | 两数值时 `R[A]=(long)RK(B) & (long)RK(C)` |
| BOR | A B C | 两数值时按位或 |
| BXOR | A B C | 两数值时按位异或 |

非数值时这些分支不赋新值，也不调用位运算元方法。`long` 在目标编译器上的宽度会影响中间运算，不能把 64 位主机的结果直接当作 32 位运行时实测。

### 1.1 命名函数和指令是不同层

`[源码引用]` full [mythroad.c](mythroad/mythroad.c) 同时将 `_and/_or/_xor/_mod/_not` 注册为 C 函数。`_and/_or/_xor` 分别对两个整数做对应 C 位运算，`_mod` 做 C 整数取余；`_not` 则使用 `!n`，结果为逻辑 0/1，**不是** `OP_BNOT` 的按位取反。除零等 C 边界不能由函数名称推断为安全行为。

`[实测：静态字节码]` `MRPPT.mrp`（SHA-256 `9d4fb1bc6b9869dacf41e936813418d616160b003f5806df693f3cd044ef0c19`）入口的 Proto `0.15` 在零基 PC 10 取全局 `_mod`，PC 14 以同一寄存器作 `CALL`；Proto `0.54` 的 PC 75 是 `BAND`。按解析器的指令字统计，663 个已解析入口中两个包出现 `BAND`，未见其余三种新增位指令。静态指令不证明分支被执行或全局值未被覆盖。

`[实测：自写探针]` 用本仓库 VM 的解析器编译并执行同一自写源码，语法 `& | ^ ~` 各产生一条对应位指令，`_and/_or/_xor/_not/_mod` 则通过 `GETGLOBAL/CALL` 调用本地 C 桩；编译后的 chunk 重新加载也得到相同结果。实验步骤和局限见第 7 节。

`[待验证]` 尚未运行官方编译器；上述结果不能推广为所有官方编译器的产物，也不能证明真实游戏调用了这些分支。

## 2. 源码语言及元方法差异

[mr_lex.c](mythroad/src/parser/mr_lex.c)、[mr_parser.c](mythroad/src/parser/mr_parser.c)、[mr_code.c](mythroad/src/parser/mr_code.c)的活跃分支表明：

| 意义 | Lua 5.0.3 | 此解析器 |
|---|---|---|
| 函数 / elseif 关键字 | `function` / `elseif` | `def` / `elif` |
| 布尔与、或、非 | `and` / `or` / `not` | `&&` / `||` / `!` |
| 不等于 | `~=` | `!=`；处理 `~=` 的代码在 `#if 0` |
| 幂 | `^` | `#` 解析为 `OP_POW` |
| 按位非、与、或、异或 | 标准 5.0 无对应指令 | `~`、`&`、`|`、`^` |
| 位运算优先级 | — | 三个二元位运算均为 `{6,6}`，与加减同优先级；一元优先级 8 |

词法表中的标准英文关键字未作为这些保留字注册；因此仅替换 chunk 魔数不能让一般 Lua 源程序按原语义运行。表描述源码编译器，而不是声称每个现有字节码都由这份编译器生成。

`mr_T_init` 把 `TM_POW` 名称设置为 `__op`；官方 5.0.3 为 `__pow`。两者执行数值 `OP_POW` 时都查询各自的**全局函数**并调用；MRP 并未在这个分支内直接调用 libc `pow`。对于非数值，其对应的元方法名称也随之改变。用户字节码使用此指令的真实动态调用结果仍应实测。

## 3. 数值、头部与 Proto

| 项 | Lua 5.0.3 默认 | 仓库 Mythroad 默认 |
|---|---|---|
| 签名 | `1b 4c 75 61` | `1b 4d 52 50` |
| 支持版本 | `0x50` | `0x50..0x80` |
| `Number` | 通常 `double` | [mr_user_number.h](mythroad/src/h/mr_user_number.h) 默认 `USE_INT`，`int` |
| `0x50` 头 | 端序、int/size_t/指令/字段/number 宽度及测试数 | 有同类检查，但要求本构建的尺寸和整数数值测试 |
| `0x51..0x80` 头 | 不接受 | 端序后直接进入 Proto；**不携带宽度自描述检查** |
| 字符串/Proto | `size_t`、终止 NUL、递归子函数 | 字段顺序基本同源，默认使用本机构建宽度 |

`OP_DIV` 对整数 `Number` 执行 C 整数除法，不等于默认浮点 Lua 5.0；除零和有符号溢出不能假定可移植。某些构建可改 `USE_DOUBLE/FLOAT/LONG`，不能将当前仓库配置推广为所有设备。头、Proto 各字段和边界参见 [MRP_FORMAT_SPEC.md](MRP_FORMAT_SPEC.md)。

## 4. C API 与协程

公共头名称粗比得到 94 个同后缀符号/宏，主要规则是 `lua_* → mrp_*`、`luaL_* → mr_L_*`、`luaV_* → mr_V_*` 等；不是完整签名和 ABI 等价性证明。以下是对接高频例子：

| Lua | Mythroad | 备注 |
|---|---|---|
| `lua_State` | `mrp_State` | 运行时状态类型 |
| `lua_open` / `lua_close` | `mrp_open` / `mrp_close` | 创建 / 关闭 |
| `lua_gettop` / `lua_settop` | `mrp_gettop` / `mrp_settop` | 栈 |
| `lua_pushnumber` / `lua_tonumber` | `mrp_pushnumber` / `mrp_tonumber` | 默认整数，ABI 不同 |
| `lua_pushstring` / `lua_tostring` | `mrp_pushstring` / `mrp_tostring` | 字符串 |
| `lua_pcall` / `lua_load` | `mrp_pcall` / `mrp_load` | 调用 / 加载 |
| `lua_resume` / `lua_yield` | `mrp_resume` / `mrp_yield` | 协程 |
| `luaL_loadfile` / `luaL_loadbuffer` | `mr_L_loadfile` / `mr_L_loadbuffer` | 读取接口被宿主替换 |

`lua.h` 与 [mr.h](mythroad/include/mr.h) 中 94 个小写共同后缀如下，每个分别套用 `lua_` 和 `mrp_` 前缀；列表同时包含声明、宏及条件编译中的名称，未做签名逐项认证：

```text
atpanic boxpointer call checkstack close concat cpcall dump equal error
getfenv getgccount getgcthreshold getglobal gethook gethookcount gethookmask
getinfo getlocal getmetatable getref getregistry getstack gettable gettop
getupvalue insert isboolean iscfunction isfunction islightuserdata isnil
isnone isnoneornil isnumber isstring istable isuserdata lessthan load
newtable newthread newuserdata next open pcall pop pushboolean pushcclosure
pushcfunction pushfstring pushlightuserdata pushliteral pushlstring pushnil
pushnumber pushstring pushupvalues pushvalue pushvfstring rawequal rawget
rawgeti rawset rawseti ref register remove replace resume setfenv
setgcthreshold setglobal sethook setlocal setmetatable settable settop
setupvalue strlen toboolean tocfunction tonumber topointer tostring tothread
touserdata type typename unboxpointer unref upvalueindex xmove yield
```

另外有 7 个共同类型后缀 `CFunction/Chunkreader/Chunkwriter/Debug/Hook/Number/State`。即使名称对应，`Number` 和结构体布局等也可能不兼容。

Mythroad 头中额外可见 `mrp_shorttypename`、`mrp_tostring_t`、`mrp_strlen_t`；官方头另有 `lua_version`。`*_t` 在 table 情况下把第 1 项数值转换为指针、把第 2 项视为长度，这是平台扩展而非普通 Lua 字符串，尤其依赖指针和数值宽度。详见 [mr_api.c](mythroad/src/mr_api.c)、[mr_auxlib.c](mythroad/src/lib/mr_auxlib.c)。

`mrp_resume` 和 `mrp_yield` 的控制流与 5.0.3 同源：死协程/非挂起协程报错，保护执行，保存 hook 状态；不能跨 C 调用/元方法边界 yield，yield 结果放到栈上，恢复时完成中断调用。`mr_baselib.c` 的 coroutine 包装会调用两者。错误文本有差异；未逐一运行协程/错误/hook 的所有边界，因此只说控制流相似，不声称行为全面等价。

## 5. GC 的确定差异

两者都是全量 mark/sweep，处理弱表和 userdata `__gc`，不是增量 GC；公式均为 `GCthreshold = 2*nblocks - deadmem`，但 **`deadmem` 内容不同**：

- 官方 5.0.3 `propagatemarks` 返回其遍历到的 table、closure、thread 大小；标记待 finalizer 的 userdata 后用 `deadmem += propagatemarks(...)`。
- 本仓库 [mr_gc.c](mythroad/src/mr_gc.c) 的 `propagatemarks` 返回 `void`，第二次只传播标记，`deadmem` 只包含 `mr_C_separateudata` 返回的 userdata 大小，不加入被其保留对象的大小。

因此相同其他条件下，若 finalizer 间接保留对象，MRP 公式中的 `deadmem` 可能更小，后续 GC 阈值可能更高；实际停顿与触发间隔需构造 finalizer/弱表样本测量，不能仅按池容量推导。空 finalizer 或未触发保留分支不一定出现差异。

## 6. 库与逐文件粗对照

完整启动路径默认打开 base、string、table、target file；socket 有条件打开，debug 也受配置限制。`mr_auxlib` 为 Lua auxiliary API 的适配，`mr_auxiliar` 为 socket 风格的 class/metatable 帮助，不是标准 Lua 5.0 核心一部分。

| 库 | 本仓库可见差异 |
|---|---|
| base | 注册 `_error/_getTab/_setTab/_iPairs/_num/_str/_gc/_loadFile/_execFile/_loadBuf` 等别名；保留部分 `tonumber/tostring/type/next/pcall/loadfile/dofile` |
| string | 增加 `clen/wlen/cstr/wstr/c2u/u2c/pack/unpack/packLen/update/pupdate/new/set/subV` 等，并有 `findEx/subEx` 命名；由具体启动配置决定可用性 |
| table | 增加 `getArrSize/setArrSize/forEach/iforEach/save/load/rawGet/rawSet` 等；保留 `getn/setn/foreach/foreachi` 等 |
| file/sys/gui | `mr_iolib_target.c` 基于 MRP 宿主操作，不等于标准 `io/os` |
| socket/tcp | `mr_socket_target.c`、`mr_tcp_target.c` 对接平台网络；可能未编入，或底层返回失败 |
| math/debug/标准 io | 仓库有 `lib/unused` 副本，不说明 full 启动默认打开这些库 |

归一化后的 token 多重集重合率仅用于定位变化，**不是**代码相似度、正确率或等价性证明。采样统计：

| 核心文件 | 比值 | 核心文件 | 比值 |
|---|---:|---|---:|
| api | 0.967 | debug | 0.986 |
| do | 0.988 | dump | 0.984 |
| func | 0.958 | gc | 0.983 |
| mem | 0.970 | object | 0.839 |
| opcodes | 0.839 | state | 0.997 |
| string | 0.996 | table | 0.994 |
| tm | 0.681 | undump | 0.967 |
| vm | 0.966 | zio | 0.994 |

该比较将注释和 include 删除，统一 `luaX_`/`lua_`/`LUA_` 常见前缀，按 C 风格 token 的**频次交集**计算 `2×交集/(两侧总数)`；未做预处理、宏展开、控制流或头文件 ABI 分析。比值高不排除第 5 节这样的重要差异；低可能只来自常量表、平台代码和条件编译。

## 7. 动态实验、编译器限制与待验证项

### 7.1 自写位运算探针

`[实测]` [mrp_vm_probe.c](../../tools/mrp_vm_probe.c) 将归档 VM 和 parser 链接到自写内存/字符分类桩及五个命名位函数桩，不加载 DSM、MRP 包或 EXT。输入为 `local x,y=6,3` 后依次返回 `x&y, x|y, x^y, ~x, _and(x,y), _or(x,y), _xor(x,y), _not(x), _mod(x,4)`。桩函数按 full 源码实现这组正常输入的算术行为，不能代替对完整运行时的动态验证。

在 Windows x86、MSVC 19.51.36260、`/std:c11 /Od /Gy` 下，源码执行与 dump 后重新加载执行均输出 `2 7 5 -7 2 7 5 0 2`。生成的 [start.mr](../../build/mrp-vm-probe-v2/start.mr) 为 360 字节，SHA-256 `ff96e86a76e80e7a0285950336e7784894b57e0a58c8b18b8e32089460f3a46f`；独立解析结果为 `BNOT/BAND/BOR/BXOR` 各 1 条、`GETGLOBAL/CALL` 各 5 条，五次全局取值依次为 `_and/_or/_xor/_not/_mod`。生成两次字节相同；输出已存在时程序拒绝覆盖。

从仓库根执行以下 PowerShell 命令可在**新目录**重建，要求安装 Visual Studio x86 C 构建工具；脚本通过 `vswhere` 查找，也可用 `-VcVarsAll` 指定 `vcvarsall.bat`：

```powershell
.\tools\build_mrp_vm_probe.ps1 -OutputDirectory build\mrp-vm-probe-repro
python tools\mrp_tool.py const build\mrp-vm-probe-repro\start.mr --output build\mrp-vm-probe-repro\constants.json
```

此文件只是自写字节码测试夹具，**不是官方工具编译产物、可交互游戏或运行兼容认证**。

### 7.2 MSVC 优化构建异常

`[实测]` 同一 x86 MSVC 版本以 `/O2` 构建上述探针时，解析源码阶段崩溃；AddressSanitizer 显示 [mr_table.c:273](mythroad/src/mr_table.c#L273) 在表重分配时读取空地址。仅将该文件改用 `/Od`、其余文件保持 `/O2`，同一探针通过。对**临时副本**插入日志显示：`computesizes` 输入数组元素数为 1，最终 `n=-1`，但输出数组长度为 `-2147483648`；未修改归档 VM 源码。

`[实测]` [独立缩减测试](../../tools/mrp_compiler_shift_probe.c)复现了同一表达式 `n == -1 ? 0 : 1 << n`：输入 `-1` 时，MSVC 19.51.36260 x86 `/Od` 输出 0，`/O2` 输出 `-2147483648`，GCC 13.2.0 x64 `-O2` 输出 0。该 MSVC 版本的 `/O2 /FA` 汇编对缩减函数只保留 `shl` 而没有 `n == -1` 判断；C 条件运算符在该输入下不执行右侧移位。此结果定位到**此工具链组合的优化代码生成异常**，并非证明所有编译器或目标设备有同样问题；GCC 测试只验证缩减表达式，不是 64 位 VM ABI 验证。探针脚本固定使用 `/Od`，不能据此认定优化构建已可用。

### 7.3 WASM 源码方言与事件探针

`[实测]` [完整探针](../../tools/mrp_dialect_probe.mr)以源码形式装入 `start.mr`，在当前 mrpx WASM 中直接启动；每个片段分别经 `_loadBuf` 编译、`_pCall` 执行。结果为 **22 个成功、9 个编译错误、0 个运行错误**。下表只表示所列片段和输入的结果；`ok` 是调用成功及其返回值，不证明其他边界、完整语义或全部 VM 版本兼容。

| 用例 | 状态 | 返回值 |
|---|---|---|
| `c_line_comment` | 成功 | `11` |
| `c_block_comment` | 成功 | `12` |
| `lua_line_comment` | 编译错误 | — |
| `lua_block_comment` | 编译错误 | — |
| `local_arithmetic` | 成功 | `7` |
| `def_function` | 成功 | `8` |
| `lua_function` | 编译错误 | — |
| `table_index` | 成功 | `7` |
| `numeric_for` | 成功 | `6` |
| `while_loop` | 成功 | `3` |
| `repeat_loop` | 成功 | `3` |
| `string_concat` | 成功 | `abcd` |
| `long_string` | 成功 | `hello` |
| `escaped_string` | 成功 | `true` |
| `logical_symbols` | 成功 | `true` |
| `logical_words` | 编译错误 | — |
| `symbol_not` | 成功 | `true` |
| `word_not` | 编译错误 | — |
| `symbol_ne` | 成功 | `true` |
| `lua_ne` | 编译错误 | — |
| `elif_keyword` | 成功 | `2` |
| `elseif_keyword` | 编译错误 | — |
| `bit_and` | 成功 | `2` |
| `bit_or` | 成功 | `7` |
| `bit_xor` | 成功 | `5` |
| `bit_not` | 成功 | `-7` |
| `hex_number` | 成功 | `42` |
| `fraction_number` | 编译错误 | — |
| `hash_length` | 编译错误 | — |
| `table_getn` | 成功 | `2` |
| `generic_table_for` | 成功 | `9` |

例如 `logical_words` 仅测 `true and false`；`escaped_string` 只比较两个相同字面量，不单独证明转义值；`fraction_number` 仅测 `1.5`。详细片段和错误文字分别见探针、[CSV](../../build/mrp-dialect-evidence/results-final/dialect-results.csv) 与 [JSON](../../build/mrp-dialect-evidence/results-final/dialect-results.json)；[原始浏览器日志](../../build/mrp-dialect-evidence/browser-console.log)的 SHA-256 为 `d3c915a807213ecc870fb8390feefeffea7301e0761a064ec4b0501d14f1ff99`。

`[实测]` 向页面队列发送右软键 18 的按下、释放，探针的全局 `dealevent(code,p0,p1)` 依次收到 `(0,18,0)`、`(1,18,0)`，与 [事件枚举](../../include/vmrp/types.h)中的按下/释放值一致。用户的简单源码没有该函数，按键时日志出现 `dealevent is nil!`；[归档实现](mythroad/mythroad.c)也查找全局 `dealevent`，不是名为 `de` 的函数。仅覆盖这一个按键和一次投递。

`[环境]` 浏览器为 Windows 上 VS Code/Electron（Chrome 150.0.7871.250、Electron 43.8.10）；源码探针 SHA-256 `40186cfd24967ad93c87e88e5f09ebf250ba1ec2ceaa6ded5331b1e0dd161368`，包 `d1c155cc2ca075dc97817c7b1daa756ab05631e2030552ce9cd1cf9c061d1612`，`mrpx.wasm` `caca4a0b84be07210b9daad6678365fbb868f7e9700416c1180ffeaaf3674960`，平台 Guest `cfunction.ext` `8f85e3cf8f0ed4a8e09eb658f9daba566989fbc06c510e4e76cd474dd275cad5`。此 Guest 是平台运行时，不是被测包内的应用 EXT；包没有 `mythroad` 条目、源码直接存储。重新打包同一源码两次所得包哈希一致，换行或构建资源变化会改变哈希。

在仓库根执行，要求已有可用的 `build\web` WASM 构建，并使用新路径避免覆盖：

```powershell
$src = 'build\mrp-dialect-repro\src'
if (Test-Path -LiteralPath 'build\mrp-dialect-repro') { throw 'Output already exists' }
New-Item -ItemType Directory -Path $src -Force | Out-Null
Copy-Item -LiteralPath 'tools\mrp_dialect_probe.mr' -Destination (Join-Path $src 'start.mr')
python tools\mrp_tool.py pack $src build\mrp-dialect-repro\probe.mrp
if ($LASTEXITCODE -ne 0) { throw 'Pack failed' }
python -m http.server 18791 --bind 127.0.0.1 --directory .
```

浏览器打开 `http://127.0.0.1:18791/build/web/main.html?f=../mrp-dialect-repro/probe.mrp&name=probe.mrp`。等待 `DIALECT|complete|31`，可点击右软键验证事件，再点击“复制日志”。在另一个 PowerShell 中将剪贴板保存为新的 UTF-8 文件，并生成结果：

```powershell
$log = 'build\mrp-dialect-repro\probe.log'
if (Test-Path -LiteralPath $log) { throw 'Log already exists' }
Get-Clipboard | Set-Content -LiteralPath $log -Encoding UTF8
python tools\mrp_dialect_results.py $log --output build\mrp-dialect-repro\results
```

转换器拒绝不完整、重复或格式异常的探针结果，也不覆盖已有 JSON/CSV；完成后停止临时 HTTP 服务。`build` 是本地生成目录，不是版本管理中的固定输入。

### 7.4 归档 VM 的幂运算钩子

`[实测：归档 VM]` 在第 7.1 节的独立探针中增加八个隔离状态的 `#` 实验；每个状态只运行表中所示片段。探针在 `mrp_open` 前调用 `mr_tm_init`，与 full 的 [DSM 初始化](mythroad/dsm.c)一致；否则归档 VM 的类型名称未初始化，构造类型错误时探针会崩溃。这一崩溃是独立探针遗漏初始化，不能作为实际 Guest 缺陷。

| 片段和探针配置 | 实际结果 |
|---|---|
| `2 # 3`，没有全局 `__op` | 运行错误 `err:1020` |
| `2 # 3`，全局 `__op` 是数字 | 运行错误 `err:1020` |
| `2 # 3`，全局 `__op` 是 C 桩 | 调用一次，收到数字 `2, 3`，返回 `203` |
| `'2' # 3`，全局 `__op` 是 C 桩 | 调用一次，收到数字 `2, 3`，返回 `203` |
| `a # 3`，`a` 的元表含 `__op`，另有全局 `__op` | 调用左侧元方法，返回 `301`；未调用全局桩 |
| `2 # b`，`b` 的元表含 `__op`，另有全局 `__op` | 调用右侧元方法，返回 `302`；未调用全局桩 |
| `a # b`，两侧元表各有 `__op` | 调用左侧元方法，返回 `301` |
| `a # 3`，`a` 的元表只有 `__pow` | 运行类型错误，未调用该元方法 |

`203/301/302` 是实验 C 桩的标记值，**不是幂运算的数值结果**。`[源码]` 数值路径先尝试将两侧转换为数字，再查找全局 `__op`；任一侧不能转换时则查找元表 `__op`，先左后右。实验只覆盖表列输入，不证明所有转换、元方法或错误边界。

在 Windows x86、MSVC 19.44.35222、`/Od` 下运行 [构建脚本](../../tools/build_mrp_vm_probe.ps1)，八项通过，原有位运算源码及字节码测试也通过。复用第 7.1 节命令并指定新的输出目录；脚本会拒绝覆盖已有目录。这是归档源码、解析器和自写 C 桩的结果，**不是**当前 `cfunction.ext`、官方编译器或真实游戏的动态验证；尚未确认目标运行时是否提供全局 `__op` 及其实现。

### 7.5 尚未完成

仓库 `luadec/luadec.c` 是集成入口，不是现成独立 CLI；归档 Makefile 引用的 `main.c` 和 `src/mr_noparser.c` 在当前归档内缺失，本机 `gcc -m32` 的链接库也缺失。**尚未运行反编译器、官方编译器、历史官方模拟器或第三方游戏**；本节动态证据来自独立归档 VM、自写探针及当前 mrpx WASM 中的自写探针和用户源码包。后续应在隔离环境使用可信工具覆盖实际 Guest 幂钩子、协程、finalizer 和 `0x50/0x80` 加载，并另行验证优化构建；不要将标准 Lua 5.0 的结果冒充 MRP 实测。
