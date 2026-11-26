# Mythroad 运行时行为规范

## 0. 范围和约定

此文描述仓库内 [full 运行时](mythroad/mythroad.c)、[DSM 中间层](mythroad/dsm.c)、[宿主函数表](../../include/vmrp/dsm.h)及 [SDK 入口](mythroad/sdk/main.c)的源码行为。编译宏、不同设备和现有预编译 Guest 可能不同；不把源码推论当成已经运行的动态实测。只记录 SMS 配置结构，不复现发送、计费或处理逻辑。

返回值按 [mrporting.h](mythroad/include/mrporting.h)：`MR_SUCCESS=0`、`MR_FAILED=-1`、`MR_IGNORE=1`、`MR_WAITING=2`。下文 `_com`/`_strCom` 适用 full 版本；未列出的 code 在该 switch 中通常保持初值 0，**不能**解释为通用能力成功。条件编译关闭的分支也可能仅返回初值。

## 1. 初始化与启动

```text
宿主                         DSM / VM                               文件
 | DSM_INIT(funcs)             |                                      |
 |---------------------------->| dsm_init: 保存函数表、起始时间       |
 |                             | 初始化 full 库、mythroad_init        |
 | MR_START_DSM(start_t*)      |                                      |
 |---------------------------->| mr_start_dsm(filename, ext, entry)   |
 |                             | 取得 screeninfo、规范 pack_filename  |
 |                             | _mr_intra_start(ext 或 start.mr)     |
 |                             | _mr_mem_init -> mem_get 一次         |
 |                             | dsm_prepare -> 工作目录/字体/编码表  |
 |                             | mrp_open；base/string/table/file     |
 |                             | 注册 _loadPack/_runFile/_com/...     |
 |                             | 设置 _mr_entry/_mr_param，状态 RUN   |
 |                             | mrp_dofile -> mr_L_loadfile          |
 |                             | _mr_readFile(name, lookfor=0) ------>|
 |                             | mrp_load -> protected parser/undump  |
 |                             | _mr_pcall 执行顶层脚本               |
```

`dsm_init` 本身返回 `VMRP_VER`，不是 `mr_start_dsm` 的成功码。[mr_auxlib.c](mythroad/src/lib/mr_auxlib.c)的 `mr_L_loadfile` 通过 `_mr_readFile` 获得完整内容；`mrp_load` 的 chunk 类型决定调用 `mr_U_undump`，随后释放读取缓冲。`entry == NULL` 时 `_mr_entry` 为 `"_dsm"`；`ext == NULL` 时入口名称为 `start.mr`。`mr_start_dsm` 不保证隐式尝试 `start.m`，目录扫描工具的第二名称只是显式筛选规则。

`filename` 前缀：`*` 原样表示已注册内存包；`%` 去掉前缀并在逗号处截取包名；`#<` 去掉前两个字符；其他原样保存。`$` 在读取阶段表示 RAM 包。`%` 分支会临时修改输入字符串，调用者应提供可写内存。失败时 `_mr_intra_start` 会停止并调用错误处理；这并不证明桌面 Guest 当前具有相同调用链。

### 1.1 子应用与回退：full

`[源码引用]` `_strCom(3, oldPackage, oldEntry)` 在 `_mr_TestCom1` 中设置**回退**包名和入口，缺省入口为 `MR_START_FILE`；它本身不启动子应用。`_runFile(package, entry, parameter)` 则更新目标包、入口和参数，设置 `MR_STATE_RESTART` 并安排 100 ms 的 restart 定时器；`mr_timer` 在该状态停止当前 VM，再调用 `_mr_intra_start`。新 VM 将参数设置为全局 `_mr_param`。

`_exit` 在存在回退包名时恢复包和入口并同样安排 restart；无回退包时走宿主退出。代码没有在此回退分支显式重置 `start_fileparameter`，但完整重启/回退链上参数的最终值和回退层数仍须构造实验，不能据静态路径保证。

### 1.2 mini 不是 full 的精简 Lua 配置

`[源码引用]` [mythroad_mini.c](mythroad/mythroad_mini.c) 的 `_mr_intra_start` 将 `vm_state` 置空并调用 `mr_doExt(appExName)`，失败再尝试 `logo.ext`；正常 `mr_event`/`mr_timer` 经 `_mr_TestComC(801,...)` 转发给原生 EXT，而不是调用 full 的 Lua `dealevent`/定时器全局。mini 的 `_strCom(3)` 固定回退入口为 `MR_START_FILE`，与 full 的可选第三参数不同。

mini 的 `mr_start_dsm` 在 `filename` 以 `%` 开头时对 `entry` 查找逗号并取 `entry+1`，full 的对应分支处理 `filename`。这是一处源码差异；输入为空、边界及设备实际行为待隔离实验，不把 mini 作为纯 Lua 可运行证据。

## 2. 事件和回调

### 2.1 SDK 外层消息

枚举顺序和转发见 [dsm.h](../../include/vmrp/dsm.h)及 [sdk/main.c](mythroad/sdk/main.c)。`param0` 在原 32 位 ABI 中承载指针；不能直接把 64 位宿主指针截断成 `int32`。

| 外层码 | 调用 | 参数 |
|---:|---|---|
| -100 `DSM_INIT` | `dsm_init` | `param0` 指向 `DSM_REQUIRE_FUNCS` |
| -99 `MR_START_DSM` | `mr_start_dsm` | `param0` 指向 `start_t{filename,ext,entry}` |
| -98 `MR_PAUSEAPP` | `mr_pauseApp` | 无有效参数 |
| -97 `MR_RESUMEAPP` | `mr_resumeApp` | 无有效参数 |
| -96 `MR_TIMER` | `mr_timer` | 无有效参数 |
| -95 `MR_EVENT` | `mr_event` | `param0` 指向 `event_t{code,p0,p1}` |

### 2.2 `mr_event` 内层消息

`mr_event(type,p0,p1)` 仅在 RUN，或允许暂停期间运行定时器的 PAUSE 状态下转发。原生 `mr_event_function` 存在时先调用；只有返回 `MR_IGNORE` 才继续查询 Lua 全局 `dealevent(type,p0,p1)`。下表值来自枚举；没有源码证据的位置标为未固定，不把猜测当作 ABI。

| code | 名称 | 已知参数 |
|---:|---|---|
| 0 | `MR_KEY_PRESS` | `p0` 按键码；桌面宿主 `p1=0` |
| 1 | `MR_KEY_RELEASE` | 同上 |
| 2 | `MR_MOUSE_DOWN` | 桌面宿主 `p0=x,p1=y` |
| 3 | `MR_MOUSE_UP` | 同上 |
| 4 | `MR_MENU_SELECT` | 菜单参数依宿主，待验证 |
| 5 | `MR_MENU_RETURN` | 参数待验证 |
| 6 | `MR_DIALOG_EVENT` | `p0` 按钮 ID；现宿主示例 `p1=0` |
| 7 | `MR_SMS_INDICATION` | 平台相关，不实现 |
| 8 | `MR_EVENT_EXIT`/`MR_EXIT_EVENT` | 退出；`mr_stop_ex` 直接调用 `dealevent(8)` |
| 9 | `MR_SMS_RESULT` | 平台相关，不实现 |
| 10 | `MR_LOCALUI_EVENT` | 本地 UI 结果，细节待验证 |
| 11 | `MR_OSD_EVENT` | 参数待验证 |
| 12 | `MR_MOUSE_MOVE` | 桌面宿主 `p0=x,p1=y` |
| 13 | `MR_ERROR_EVENT` | 执行异常通知，参数待验证 |
| 14 | `MR_PHB_EVENT` | 通讯录事件，参数待验证 |
| 15 | `MR_SMS_OP_EVENT` | 参数待验证，不实现 |
| 16 | `MR_SMS_GET_SC` | 参数待验证，不实现 |
| 17 | `MR_DATA_ACCOUNT_EVENT` | 参数待验证 |
| 18 | `MR_MOTION_EVENT` | 参数待验证 |

键盘枚举中 `0..9` 对应数字，`10/11` 为 `*`/`#`，`12..15` 为上/下/左/右，`17/18` 左/右软键，`20` 选择，`24/25` A/B。定义见 [mrporting.h](mythroad/include/mrporting.h)，勿与物理键值混用。

`[官方文档]` `mr_event.htm` 另列 `MR_MCHAT_EVENT=100`，当前归档头未定义此名字；full 的通用转发不按 code 白名单过滤，所以未定义不代表 100 一定不可转发。文档指出平台返回值不能当作应用 `mrc_event` 返回值；源码的原生 hook 返回非 `MR_IGNORE` 时直接返回该值，Lua 回调则以零返回值调用并最终返回 `MR_SUCCESS`。原生 hook 与应用回调的具体关系待测，参见 [文档笔记](OFFICIAL_DOCS_NOTES.md)。

`mr_timer` 要求当前定时器状态 RUNNING，先置 IDLE；原生 timer 回调返回 `MR_IGNORE` 时，才查找 `mr_timer_p` 所指名称的 Lua 全局并调用。暂停/恢复分别可能调用原生 hook 和 Lua `suspend()`/`resume()`，并暂停/重启定时器；重启状态走特殊分支。共享 VM 状态的并发可重入行为尚无证明。

## 3. `_mr_readFile`：磁盘、包与路径

**源码：**[mythroad.c 的读取器](mythroad/mythroad.c)及 [dsm.c 的路径处理](mythroad/dsm.c)。

| 情况 | 行为 |
|---|---|
| 普通磁盘包、`lookfor=0` | 先尝试 `readFile(filename)` 读取当前工作目录中的外部文件；存在时直接返回，可能覆盖包内同名资源。否则打开 `pack_filename` 并查索引 |
| `lookfor=1` | 包内存在性探测；不走上述外部文件优先分支，成功返回非空哨兵 |
| `lookfor=2` | 按调用分支取得资源指针/长度；ROM/RAM 分支可直接返回原始指针，不能概括为所有磁盘路径均零拷贝 |
| `pack_filename=*A` 等 | 使用 `mr_m0_files[letter-'A']` 中已注册内存包，顺序遍历记录 |
| `pack_filename=$...` | 使用 `mr_ram_file` 和相应长度，顺序遍历记录 |
| 旧式磁盘包 | `headbuf[1] <= 232` 时走顺序记录，而非新版索引 |

`x:/` 等盘符由 `dsmSwitchPath` 的**工作目录切换**实现：`X` → `mythroad/disk/x/`，A/B → 对应隐藏目录和子路径，C → 输入子路径，Z → 默认 `mythroad/`，Y → 查询。`get_filename` 拼接工作目录并规范分隔符，`FLAG_USE_UTF8_FS` 再转换到 UTF-8；不是简单地在 `_mr_readFile` 中把任意 `x:/` 子串转换为宿主绝对路径。`*://` 没有在这份读取器中发现通用嵌套路径语义，需真实调用实验。磁盘同名文件优先级应以受控样本验证，不能直接把这个实验应用于原始语料。

`[官方文档]` `mr_platEx(1204)` 还列出了外插盘 D；当前 DSM 未实现 D，走默认 `MR_IGNORE`。卷二存档规范要求从包信息获得短名，并以该短名建立存档目录；这与 `_mr_readFile` 的包内查找不是同一个机制。

`DSM_REQUIRE_FUNCS.flags` 已定义且有使用的位：bit 0 `FLAG_USE_UTF8_FS` 控制文件名转换；bit 1 `FLAG_USE_UTF8_EDIT` 控制编辑框文本转换。其他位没有在此实现中发现定义，不能断言所有历史运行时都未使用。

## 4. `_com` → `_mr_TestCom` 全部分支

表按 [switch](mythroad/mythroad.c)列出每个实际 case。除特殊返回外，`L != NULL` 时把 `ret` 推入 Lua 并返回一个结果；原生直接调用且 `L == NULL` 时返回 0。

| code | 行为 / 返回或条件 |
|---:|---|
| 1 | `mr_getTime()` |
| 2 / 3 | 设置原生 event / timer 函数指针 |
| 4 / 5 / 6 | 设置 stop / pause / resume 原生指针 |
| 7 / 8 | 相应 `MR_PLAT_DRAWTEXT` / `MR_VIA_MOD` 宏下直接返回 `input1` |
| 100 / 101 / 102 | 返回 `LG_mem_min` / `LG_mem_top` / `LG_mem_left` |
| 200 | RUN 且震动允许时启动震动；否则返回成功 |
| 201 | 停止震动调用被注释，当前分支不操作 |
| 300 / 301 | 设置声音 / 震动开关 |
| 302 / 303 | 设置 / 清除 `MR_FLAGS_RI` |
| 304 / 305 | 设置 / 清除 `MR_FLAGS_EI` |
| 306 / 307 | 设置 / 清除 SMS 返回标志；本研究不复现处理 |
| 400 | 睡眠 `input1` |
| 401 | 返回旧最大屏宽并赋新值 |
| 402 | `MR_SOCKET_SUPPORT` 下打开 socket 库 |
| 403 | `mrp_setgcthreshold(input1)` |
| 404 | SIM 状态通知，平台依赖 |
| 405 | 关闭网络并在 socket 表存在时置状态 CLOSED |
| 406 | 返回旧屏高并赋新值 |
| 407 | 设置暂停期间 timer 开关，调用 `mr_plat(1202,input1)` |
| 408 | 按当前缓冲类型分配或检查指定字节数 |
| 500 | 条件编译下读取 SMS 配置；不复现业务 |
| 501 | 条件编译下读取配置字节 |
| 502 | 条件编译下写入配置字节 |
| 503 | 条件编译下读取配置中的编码标志 |
| 504 | 保存 SMS 配置；不复现业务 |
| 3629 | `input1==2913` 时设置 `MR_FLAGS_BI` |
| 3921 | `input1==98352` 时设置 `MR_FLAGS_AI` |
| 3251 | `input1==648826` 时清除 `MR_FLAGS_AI` |

## 5. `_strCom` → `_mr_TestCom1` 全部分支

| code | 行为 / 结果 |
|---:|---|
| 2 | 替换 RAM 包指针及长度，释放旧值 |
| 3 | 设置回退包名与入口名 |
| 4 | 设置启动参数字符串 |
| 5 / 6 | 设置 / 清除异常字符串指针 |
| 9 | 对给定内存范围调用 `mr_cacheSync`，无 Lua 返回值 |
| 100 | 条件编译下执行主机名查询 |
| 200 | 返回输入字节 CRC |
| 300 | 按运行时支持的压缩格式解码；未识别时返回原字符串 |
| 500 | 返回 16 字节 MD5 |
| 501 / 502 | 编码 / 解码 Base64，分配失败时无结果 |
| 600 | 读取已注册内存包或 RAM 包片段 |
| 601 | `_mr_readFile(...,0)`，返回内容或 nil；**本身不是执行 EXT** |
| 602 | `_mr_readFile(...,1)`，返回成功值或 nil |
| 603 | `_mr_readFile(...,2)`，成功返回指针数值与长度，否则 nil；所有权依分支 |
| 700 / 701 | SIM / SMS 通知，不复现 |
| 800 | 设置函数表并进入输入缓冲 `+8` 的 ARM 原生载入入口，调用 `fixR9` |
| 801 | 给已加载原生扩展发送事件，返回输出串与状态 |
| 802 | 设置另一修复指针并进入 ARM 原生载入入口 |
| 900 | `mr_platEx(200001, _mr_c_port_table, ...)` |

EXT 的存在或静态常量命中只能说明线索；实际依赖要观察分支及资源来源。800、801、802 明确触及原生扩展；601–603 是通用资源接口。

## 6. DSM `mr_plat` / `mr_platEx` 全部已实现 case

这两张表只覆盖 [dsm.c](mythroad/dsm.c)中**未注释的 switch case**，不是所有平台历史 code。未命中默认日志并返回 `MR_IGNORE`。

### 6.1 `mr_plat(code,param)`

| code | 返回 |
|---:|---|
| 1001 | `mr_getSocketState(param)` |
| 1205 | `MR_TOUCH_SCREEN`（1001） |
| 1206 | `MR_CHINESE`（1000） |
| 1211 | 用当前时间重置随机种子，返回 `1000 + rand() % param`；`param=0` 未防护 |
| 1218 | `MR_MSDC_OK`（1001） |

### 6.2 `mr_platEx`

| code | 返回与输出 |
|---:|---|
| 1012 / 1013 | 内部 cache 申请/释放占位，`MR_IGNORE` |
| 1014 / 1015 | 扩展内存申请/释放占位，`MR_IGNORE` |
| 1017 | 静态信号结构 `{3,5,5,1}`，`MR_SUCCESS` |
| 1116 | 静态编译时间字符串及长度，`MR_SUCCESS` |
| 1201 | 静态字符宽高打包为 32 位值，`MR_SUCCESS` |
| 1204 | `dsmSwitchPath` 的结果；Y 查询返回静态路径缓冲 |
| 1207 | UCS-2 BE → GB；若输出缓冲已给定则复制，否则分配，`MR_SUCCESS` |
| 1222 / 1223 | 背光占位，`MR_SUCCESS` |
| 1224 / 1307 | 小区 / 多 SIM 信息未实现，`MR_IGNORE` |
| 1305 | A/B/C 三盘返回静态、硬编码总量和剩余量结构；其他盘 `MR_IGNORE` |

头文件中还定义了很多 code，但没有对应的活跃 DSM case；注释中的媒体分支也不是实现。`mr_plat(1202)` 和 `_strCom(900)` 发出的 `platEx(200001)` 在本 DSM switch 中走默认分支。

## 7. 内存池和 SMS 配置格式

`_mr_mem_init` 一次调用 `mr_mem_get`，以 32 位偏移维护空闲链，分配大小向 8 字节取整、first-fit，释放时按地址插入并合并相邻块；`realloc` 重新分配、复制、释放。`LG_mem_left` 为总空闲，`LG_mem_min` 为历史最小空闲，`LG_mem_top` 为实现中的偏移高水位。**源码限制：**`len >= LG_mem_left` 被拒绝，即使恰好相等。它不是 Lua GC 自身的可用对象数，也不能单凭池大小推断 GC 暂停时间。

只作格式记录：`mr_sms_cfg_buf` 为 `120 × 36 = 4,320` 字节，已定义偏移 4、5、6、32 分别表示更新/编码等标志；get/set 检查 `pos < 0`、`pos >= 4320` 或 `pos + len >= 4320` 时失败。消息子包格式由一字节内容长度接后续字节；完整子类型和边界情况未穷尽。未发送短信、未触发计费、未复制业务逻辑。
