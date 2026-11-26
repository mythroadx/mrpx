# Mythroad / SkyEngine 移植 ESP32-S3：可行性与实施研究报告

## 阅读结论与证据边界

**结论：原生重建 Mythroad VM、替换 DSM 宿主，是可行且应优先实施的路线；目前不能承诺 664 个包全部兼容，也不能把当前桌面版运行成功视为原生源码已构建成功。**

本报告遵循“研究 → 验证清单 → 编码”的阶段边界，不修改运行时，不交付未经验证却声称可运行的整套固件。文中的 C 示例是完整源文件；第六章的 VM 集成程序明确为完整流程伪代码。ESP-IDF API 以 **v5.2.5 / esp32s3** 为核对基线，其他 5.x 版本需重新检查配置和驱动差异。本机有 PC GCC，但没有 ESP-IDF 交叉工具链和目标板，因此不声称完成固件编译或实机验收。

证据标识：

- **[用户数据]**：任务书提供的硬件、664 包统计；本研究不重新扫描全集。
- **[源码事实]**：当前工作区归档源码及共享 DSM 头文件，引用带起始行号。
- **[样本实测]**：已完成的 [阶段 0 报告](ESP32_PHASE0.md)，仅适用于其中记录 SHA-256 的包。
- **[官方依据]**：文末 ESP-IDF / Lua 官方资料。
- **[设计] / [估算]**：本报告建议或计算，不是硬件测量值。
- **[待验证]**：必须通过 PC 实验、交叉编译或目标板确认。

必须先澄清的边界：

| 任务书中的表述                                          | 本报告采用的准确解释                                                                                                                    |
| ------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| 664 包都有脚本入口                                      | **[用户数据]** 采信；不等于 664 包都不依赖原生代码。脚本可以只是 EXT 启动器。                                                     |
| 扫雷可作纯 Lua 首测                                     | **[样本实测]** 用户提供的 11,680 B 扫雷包启动时加载 ARM `cfunction.ext`；失败后退出。这个具体版本不符合“禁用 EXT”的验收前提。 |
| vmrp 已跑通 VM 源码                                     | **[源码事实]** 当前宿主加载预编译 ARM Guest；归档源码原生重建仍需验证，见 [研究目录说明](README.md)。                              |
| 标准 Lua 5.0 版本字节是 `0x51`                        | 标准 Lua 5.0 是 `0x50`；`0x51` 是 5.1。MRP 的 `0x80` 不能靠修改魔数转换为标准 Lua。                                               |
| `MR_PLAT_DRAWTEXT` 要求增加 DSM `drawText` 字段     | 共享函数表没有该字段；归档 DSM 已实现字形绘制，通过 `_DrawPoint` 写 VM 屏幕。                                                         |
| `gb16.uc2` 是 GB2312 区位字库                         | 当前实现按 **Unicode × 32** 索引；与 HZK16 区位布局不同。                                                                        |
| `mr_start_dsm("/spiffs/winmine.mrp", ...)` 可直接使用 | 归档 DSM 会再拼工作目录；应传相对包名，由宿主统一映射 VFS。                                                                             |

---

## 第一章：总体架构设计

### 1.1 原生固件架构

**[设计]**

```text
Flash：程序 + 编码常量表 + FatFS/WL（字库、安装的 MRP、存档）
                                  |
                         ESP-IDF VFS / 文件句柄表
                                  |
GPIO ISR -> 输入任务 -> 值类型事件队列 -> VM 任务（唯一 VM 调用者）
触摸 SPI -> 输入任务 -----------^       |
esp_timer TASK 回调 ------------^       +-> DSM_REQUIRE_FUNCS
       仅投递到期事件                     +-> dsm_init
                                         +-> mr_start_dsm / mr_event / mr_timer
                                         +-> Mythroad Lua 5.0 派生 VM
                                         +-> 单块 PSRAM arena
                                         |
                           PSRAM RGB565 屏幕 + 脏矩形
                                         |
                             帧互斥锁 + LCD 任务通知
                                         |
                         内部 SRAM DMA 条带（打包/换字节）
                                         |
                           esp_lcd / SPI2 / ST7789V
```

`mrc_*` 是应用侧语义名称，不意味着宿主必须重新实现同名全部 SDK 函数。实际边界是 [DSM_REQUIRE_FUNCS](../../include/vmrp/dsm.h#L32)：许多绘图、解包和脚本 API 已在 Mythroad/DSM 内实现。

### 1.2 路线比较

| 路线                             | 必须维护的内容                            | 优缺点                                                     | 决策                         |
| -------------------------------- | ----------------------------------------- | ---------------------------------------------------------- | ---------------------------- |
| 原生 VM + ESP-IDF DSM            | 归档 C 的可移植性、准确宿主语义、资源兼容 | 不解释 ARM 指令；可测 GC/内存；不支持原 ARM EXT            | **首选**               |
| ARM 指令模拟 + 原 Guest          | ARM 模拟器、原 Guest ABI、宿主系统调用    | 可能保留更多二进制行为；额外 CPU/内存成本，S3 性能尚无证明 | 后续独立研究，不作为首期前提 |
| 模拟完整 MTK 再运行 `vmrp.mrp` | 芯片/RTOS/设备、原系统服务、嵌套 MRP 宿主 | 远超需求；不是把 SDL API 换成 IDF                          | 不采用                       |

**[源码事实]** [sdk/vmrp.c:157](mythroad/sdk/vmrp.c#L157) 是运行在 MRP 平台上的宿主模板；[sdk/main.c:11](mythroad/sdk/main.c#L11) 展示外层 DSM 消息转发。ESP 原生入口替换它们，不把 SDK 示例再打包成 MRP。

### 1.3 任务、优先级与所有权

以下均为**初始设计值**，不是最优值：

| 执行者                 | 核 / 优先级                | 初始栈预算        | 责任与限制                                                   |
| ---------------------- | -------------------------- | ----------------- | ------------------------------------------------------------ |
| VM 任务                | Core 1 / 5                 | 24 KiB，内部 SRAM | 唯一执行 `dsm_init`、启动、事件、计时器、暂停、停止的任务  |
| LCD 任务               | Core 0 / 6                 | 8 KiB，内部 SRAM  | 唯一持有 LCD device 的任务；完成 DMA 后才复用缓冲            |
| 输入任务               | Core 0 / 7                 | 4 KiB，内部 SRAM  | 短时运行，5 ms 采样、连续稳定约 20 ms 去抖；投递按下/释放    |
| `esp_timer` 系统任务 | IDF 管理，不自行覆盖优先级 | IDF 管理          | 回调只写事件/溢出锁存，不调用 VM、不阻塞等待                 |
| GPIO / LCD ISR         | 中断上下文                 | 系统中断栈        | 只通知、记录完成；不访问 PSRAM，不分配，不读字体、不调用 Lua |

ESP-IDF 创建任务的栈参数按**字节**理解；实际应记录栈高水位，而不是照搬普通 FreeRTOS 移植中“字”为单位的假设。

队列初值为 64 个固定大小事件；若实际结构为 24 B，数据区为 1,536 B，另有队列控制块。队列内复制按键值、坐标、命令和 timer generation，不能保存生产者栈上的事件指针。

**帧一致性关键点：** VM 通过 `getScreenBuffer` 直接写 PSRAM，锁住 `drawBitmap` 回调本身是不够的。首版在整个 VM 启动/单次事件调用期间持有帧锁；LCD 持同一锁完成一轮脏区输出。VM 只发通知，不在持锁时等待 LCD。这样正确但会产生约一帧的阻塞；后续再引入快照缓冲与明确所有权交接。

### 1.4 候选接线

**[设计；待按实际板号核对]** 以 ESP32-S3-DevKitC-1-N16R8 类开发板为参考；用户尚未提供具体原理图，不把以下当作已验证接线。

| 外设信号                     |                GPIO | 说明                                                       |
| ---------------------------- | ------------------: | ---------------------------------------------------------- |
| LCD SCLK / MOSI              |             12 / 11 | SPI2，先 20 MHz 确认，再测 40 MHz                          |
| LCD CS / DC / RESET          |          10 / 9 / 8 | 独立控制                                                   |
| LCD BL                       |                   7 | 控制模块背光使能或晶体管；不可直接由 GPIO 为大电流背光供电 |
| 上 / 下 / 左 / 右            |      4 / 5 / 6 / 15 | 按键接地，低有效，上拉；线长较大时加外部上拉               |
| A / B                        |             16 / 17 | 按键语义用应用 profile 映射                                |
| 可选 XPT2046 CS / MISO / IRQ |        18 / 13 / 14 | 与 LCD 共 SCLK/MOSI，不同 CS；低频事务独立配置             |
| 电源 / 地                    | 模块规格要求 / 共地 | ESP32 GPIO 为 3.3 V 逻辑；确认屏模块是否自带稳压、电平转换 |

**[官方依据]** GPIO 0/3/45/46 是启动配置脚；26–32 通常由 Flash/PSRAM 使用；Octal 配置还使用 33–37；19/20 留给 USB。另保留常用串口 43/44，避免与调试冲突。具体开发板还可能占用 LED 引脚，见 [GPIO 文档][E-GPIO] 和 [开发板指南][E-BOARD]。

---

## 第二章：内存方案

### 2.1 一次性 arena，而不是替换每一次 Lua 分配

**[源码事实]**

- [mem.c:28](mythroad/mem.c#L28) 的 `_mr_mem_init` 向宿主索取一次连续区域；内部再通过按 8 B 取整的 first-fit 空闲链分配。
- [mem.c:97](mythroad/mem.c#L97) 回收合并相邻块；`realloc` 可能同时占用旧块和新块。因此“总空闲足够”不等于“最大连续块足够”。
- [mem.c:149](mythroad/mem.c#L149) 的 `mr_mallocExt` 只是带长度头的 arena 分配接口，编码等普通功能也使用它，**不能随 ARM EXT 一起删除**。
- SDK 示例是 320 KiB；当前桌面宿主另有 4 MiB pool。两者不能作为同一个内存基线。

**[设计]** 首版 VM arena 为 **2 MiB PSRAM**，压力测试可改为 4 MiB；启动时一次分配，失败即拒绝启动应用，不回退到内部 SRAM。

| 项目                            |            首版预算 | 位置           | 是否已含在 VM arena 内              |
| ------------------------------- | ------------------: | -------------- | ----------------------------------- |
| VM、脚本对象、图片/解包临时数据 |         2,097,152 B | PSRAM          | 是                                  |
| 240×320 RGB565 屏幕            | 153,600 B = 150 KiB | PSRAM          | 启用 `USE_GET_SCREEN_BUFFER` 时否 |
| 两个 240×16×2 DMA 条带        |            15,360 B | 内部 DMA SRAM  | 否                                  |
| 字形缓存，建议先 64 字          |    2,048 B + 元数据 | 内部 SRAM      | 否                                  |
| 三个应用任务栈                  |              36 KiB | 内部 SRAM      | 否                                  |
| 事件数据区                      | 约 1.5 KiB + 控制块 | 内部 SRAM      | 否                                  |
| 字库本体                        |         2,097,152 B | Flash 文件系统 | 不常驻 RAM                          |
| FatFS、日志、驱动、系统任务     |       构建/实测确定 | 分项记录       | 否                                  |

仅 arena 加屏幕合计 **2,250,752 B ≈ 2.146 MiB**。4 MiB arena 时为 4,347,904 B ≈ 4.146 MiB。不能把剩余的标称 PSRAM 全算作可分配：应读取 capabilities heap 的实际总量、空闲量和最大块；ECC、链接放置和系统分配可能减少可用空间。[E-PSRAM][E-PSRAM]

### 2.2 大堆与 GC：不是“扫完整个 PSRAM”

**[源码事实]** [mr_mem.c:59](mythroad/src/mr_mem.c#L59) 用 `G(L)->nblocks` 记录 VM 对象分配；[mr_state.c:114](mythroad/src/mr_state.c#L114) 初始化 GC 阈值；[mr_gc.c:386](mythroad/src/mr_gc.c#L386) 在收集后根据对象内存重新设置阈值。[mr_gc.c:477](mythroad/src/mr_gc.c#L477) 是同步标记、清扫、调整和终结，不是现代 Lua 的增量 GC。

因此：

1. arena 从 320 KiB 增到 2 MiB，**不会自动扫描额外的空闲字节**。
2. 更大的 arena 允许更大的存活对象图、哈希表和待清扫集合；这些实际对象，以及 PSRAM 随机访问，才会增加暂停。
3. 不应一上来增大 GC 阈值来换平均 FPS；可能使最坏暂停更严重。
4. 第一轮保留原阈值策略，测每轮 GC 用时、活跃对象字节、arena 峰值和最大连续空闲块，再决定优化。

最少记录：`LG_mem_len - LG_mem_min`、当前 `LG_mem_left`、最大连续块、`G(L)->nblocks`、GC p95/max、事件处理 p95/max、分配失败请求大小、任务栈高水位。arena 指标还包含非 Lua 图片等分配，不能用 `nblocks` 替代总峰值。

### 2.3 完整 PSRAM arena 适配示例

以下是独立组件源文件示例 `esp32_arena.c`，不是整套宿主。只允许 VM 任务调用；接口类型与 DSM 的 32 位类型对应。长度校验失败时保留原块并报错，防止错误释放。**尚未交叉编译。**

```c
#include <inttypes.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

enum { ARENA_BYTES = 2 * 1024 * 1024 };
static char *arena;
static const char *TAG = "mrp_arena";

int32_t esp32_mem_get(char **base, uint32_t *length)
{
    if (base == NULL || length == NULL) {
        ESP_LOGE(TAG, "invalid output argument");
        return -1;
    }
    *base = NULL;
    *length = 0;
    if (arena != NULL) {
        ESP_LOGE(TAG, "arena already owned");
        return -1;
    }
    arena = heap_caps_malloc(ARENA_BYTES,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (arena == NULL) {
        ESP_LOGE(TAG, "cannot allocate %u bytes", (unsigned)ARENA_BYTES);
        return -1;
    }
    *base = arena;
    *length = ARENA_BYTES;
    return 0;
}

int32_t esp32_mem_free(char *base, uint32_t length)
{
    if (arena == NULL || base != arena || length != ARENA_BYTES) {
        ESP_LOGE(TAG, "invalid arena release: %p, %" PRIu32,
                 (void *)base, length);
        return -1;
    }
    heap_caps_free(arena);
    arena = NULL;
    return 0;
}
```

### 2.4 帧缓冲与 PSRAM 带宽

| 方案                       | RAM 成本与特点                                      | 结论                            |
| -------------------------- | --------------------------------------------------- | ------------------------------- |
| 整屏内部 SRAM              | 单屏 150 KiB，与栈、驱动、DMA 争抢内部内存          | 不作默认                        |
| 整屏 PSRAM + 内部 DMA 条带 | 大容量、明确 DMA 能力；多一次读取/打包              | **首版**                  |
| PSRAM 直接 DMA             | S3 硬件存在支持，但受具体驱动、对齐、缓存和带宽限制 | 不假定所有 IDF SPI 路径都零拷贝 |
| 双 PSRAM 快照              | 再增加 150 KiB；能缩短持锁输出时间                  | 性能阶段候选                    |

ESP32-S3 不是“绝对不能从 PSRAM DMA”；但是 IDF v5.2.5 SPI Master 文档仍要求 DMA-capable 内部缓冲及对齐，否则可能分配临时缓冲。[E-SPI][E-PSRAM] 首版显式 bounce buffer，避免依赖不可见的复制和内存分配。

全屏至少一次写 PSRAM、一次读 PSRAM：30 FPS 时仅这两项约 `153600×2×30 = 9.216 MB/s`。这不是所需总带宽：还未包含图片读取、透明混合、GC、字体查表、缓存行开销。连续行填充优于随机逐像素访问；不要为每个点发 SPI 事务。热区优先放**DMA 条带、字形缓存、队列**，而不是立即把整屏搬进内部 SRAM。

### 2.5 sdkconfig 关键项

**[设计；名称按 v5.2.5 核对]**

| 配置                                                                        | 首版选择 / 条件                                                                       |
| --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`                                         | `y`                                                                                 |
| `CONFIG_SPIRAM` / `CONFIG_SPIRAM_BOOT_INIT` / `CONFIG_SPIRAM_MEMTEST` | `y`                                                                                 |
| `CONFIG_SPIRAM_MODE_OCT`                                                  | 仅实际模块为 Octal PSRAM 时 `y`；不能仅凭容量猜模式                                 |
| `CONFIG_SPIRAM_TYPE_AUTO` / `CONFIG_SPIRAM_SPEED_80M`                   | 建议自动检测、80 MHz；以模块支持为前提                                                |
| `CONFIG_SPIRAM_USE_CAPS_ALLOC`                                            | `y`，显式选择 PSRAM，避免普通分配随意进入外部内存                                   |
| `CONFIG_SPIRAM_IGNORE_NOTFOUND`                                           | `n`，PSRAM 缺失不能当正常启动                                                       |
| `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`                               | `n`                                                                                 |
| `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `CONFIG_SPIRAM_RODATA`             | 首版 `n`                                                                            |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`                                   | 仅改用 `SPIRAM_USE_MALLOC` 后适用；预留量依据栈和 DMA 实测，不与 CAPS-only 配置混用 |
| `CONFIG_FREERTOS_HZ`                                                      | 初值 1000；再对比低 tick 频率的开销与输入延迟                                         |
| `CONFIG_FREERTOS_UNICORE`                                                 | `n`                                                                                 |
| `CONFIG_PARTITION_TABLE_CUSTOM`                                           | 完整运行时使用 `y`                                                                  |
| `CONFIG_FATFS_LFN_HEAP` / `CONFIG_FATFS_MAX_LFN`                        | `y` / 255                                                                           |
| `CONFIG_FATFS_CODEPAGE_936` / `CONFIG_FATFS_API_ENCODING_ANSI_OEM`      | 首版 GBK 文件名路线，DSM `flags=0`                                                  |
| `CONFIG_FATFS_FS_LOCK`                                                    | 初值 16，结合宿主句柄数验证                                                           |
| `CONFIG_FATFS_PER_FILE_CACHE`                                             | 按 512/4096 B sector × 打开文件数核算，不忽略其内存                                  |
| `CONFIG_FATFS_ALLOC_PREFER_EXTRAM`                                        | 可保留以节约内部 SRAM，验证性能与 Flash 访问路径                                      |
| `CONFIG_SPIFFS_OBJ_NAME_LEN`                                              | 仅 SPIFFS 备选使用；建议 128，镜像生成必须同配置                                      |

PSRAM 模式和条件配置依据 [S3 Kconfig][E-KPSRAM]、[公共 PSRAM Kconfig][E-KCOMMON]；FatFS 依据 [FatFS Kconfig][E-KFAT]。不关闭看门狗掩盖长脚本；长循环的安全点/执行预算列入第七章。

---

## 第三章：宿主层逐函数实现方案

### 3.1 唯一 ABI 定义与基本回调

直接包含 [共享 dsm.h:32](../../include/vmrp/dsm.h#L32)，**不要根据任务书列表重新声明结构体**。所有编译单元统一 `USE_FINDDIR`、`USE_GET_SCREEN_BUFFER`，否则函数表布局不同。首字段 `test` 不能漏，网络/UI 等不少成员名带 `mr_` 前缀。函数表使用静态零初始化，再逐项填入真实实现或有签名的失败 stub，不能把不同签名都 cast 成同一个函数。

| 字段                       | 实现与返回约定                                                       |
| -------------------------- | -------------------------------------------------------------------- |
| `test`                   | 安全自检/日志函数，`void`；不保留未初始化指针                      |
| `log`                    | `ESP_LOGI(TAG, "%s", msg)`；不能把应用文本当格式字符串             |
| `exit`                   | 标记请求退出；VM 当前受保护调用返回后统一清理，不能从任意回调销毁 VM |
| `srand`                  | 保存可复现随机种子，按 VM 单任务调用                                 |
| `rand`                   | 使用确定的 PRNG / libc `rand`，记录实现；不改变数值范围语义        |
| `mem_get` / `mem_free` | 第二章的一次性 arena                                                 |
| `flags`                  | bit 0：UTF-8 文件路径；bit 1：UTF-8 编辑框文本；其他位未定义，置 0   |

flags 的两个已定义位见 [dsm.h:24](../../include/vmrp/dsm.h#L24)。末尾“只有文件系统”注释已落后于实际结构，不能据此忽略 bit 1。

### 3.2 文件 API 与目录

**[设计]** 优先 FatFS + wear levelling，挂载 `/mrpfs`。任务书允许 SPIFFS/FatFS；这里选择 FatFS 是因为 [dsm_prepare:905](mythroad/dsm.c#L905) 会建立目录，且应用可能枚举目录。SPIFFS 的平面名字、约 75% 可靠空间利用率及临近满盘时长 GC 是官方明确限制。[E-SPIFFS][E-SPIFFS]

| 字段                                               | POSIX / IDF 映射                     | 必须保留的 MRP 语义                                                                       |
| -------------------------------------------------- | ------------------------------------ | ----------------------------------------------------------------------------------------- |
| `open`                                           | `open` + 独立正整数句柄表          | 失败 **0**，不能直接返回 POSIX `-1`；不暴露原始 fd                                |
| `close`                                          | 查表后 `close`                     | 成功 0、失败 -1；无效/重复关闭报错                                                        |
| `read`                                           | `read`，可重试 `EINTR`           | 返回实际字节数；EOF 为 0；失败 -1                                                         |
| `write`                                          | `write`                            | 返回实际写入量；处理短写，不伪称全写                                                      |
| `seek`                                           | `lseek`                            | 成功返回 **MR_SUCCESS=0**，不是新偏移；失败 -1                                      |
| `getLen`                                         | `stat`                             | 文件长度；不存在、非文件或超过返回类型范围则失败                                          |
| `info`                                           | `stat` + 类型判断                  | 文件 `MR_IS_FILE=1`，目录 `MR_IS_DIR=2`，无效 `MR_IS_INVALID=8`；不是 POSIX 的 0/-1 |
| `remove`                                         | `unlink`                           | 不允许把目录当普通文件删除                                                                |
| `rename`                                         | 同 VFS 内 `rename`                 | 两个路径均规范化；目标存在/打开文件策略明确                                               |
| `mkDir`                                          | `mkdir`                            | 已存在且确为目录可成功；其他错误不能吞掉                                                  |
| `rmDir`                                          | `rmdir`                            | 非空目录失败；禁止删除挂载根                                                              |
| `mrc_findStart`                                  | `opendir` + 找首项，分配迭代器句柄 | 填第一个名字；无结果/失败返回 `MR_FAILED`，释放临时迭代器                               |
| `mrc_findGetNext`                                | `readdir`                          | 填下一个 basename；容量不足报错，不截断为另一个文件名；正常结束不是故障日志               |
| `mrc_findStop`                                   | `closedir` + 释放句柄              | 正确检查生命周期                                                                          |
| 非 `USE_FINDDIR` 的 `opendir/readdir/closedir` | 同一目录层的另一 ABI                 | 三者替代 find 三者，不同时放进结构体                                                      |

状态、文件模式见 [mrporting.h:331](mythroad/include/mrporting.h#L331)。`RDONLY=1`、`WRONLY=2`、`RDWR=4`、`CREATE=8`、`RECREATE=16`，不能原样传 POSIX `O_*`。明确转换读写模式、`O_CREAT`、`O_TRUNC`；`COMMITTED` 的持久化诉求用受控 `fsync`，不默默忽略。模式中的平台别名须结合实际调用验证，不能仅凭同一个数值推导新的共享语义。

SPIFFS 备选必须实现逻辑目录集合、按前缀枚举、空目录及删除语义；**不能把所有 mkdir/rmdir 永远返回成功**。首期若仅提供只读安装资源，也必须明确禁止依赖可写目录的应用，而不是冒充完整 VFS。

### 3.3 两层路径转换，不重复添加目录

**[源码事实]** [dsm.c:201](mythroad/dsm.c#L201) 定义 `mythroad/` 和 `mythroad/disk/a|b|x/`；[dsm.c:249](mythroad/dsm.c#L249) 处理切盘；[dsm.c:325](mythroad/dsm.c#L325) 的 `get_filename` 拼工作目录后再处理编码。

**[设计]**

| 输入层           | 例子                                              | 输出                                            |
| ---------------- | ------------------------------------------------- | ----------------------------------------------- |
| 启动 API         | `mr_start_dsm("winmine.mrp", "start.mr", NULL)` | DSM 得到逻辑 `mythroad/winmine.mrp`           |
| 宿主 VFS         | `mythroad/winmine.mrp`                          | `/mrpfs/mythroad/winmine.mrp`                 |
| 直接盘符兼容入口 | `x:/save.dat`                                   | `/mrpfs/mythroad/disk/x/save.dat`             |
| 相对字体路径     | `system/gb16.uc2`                               | 经 DSM 后为 `/mrpfs/mythroad/system/gb16.uc2` |

对直接传入文件 API 的 `x:/...`，需在 `get_filename` **拼工作目录之前**的 ESP 分支识别盘符，交给宿主映射；否则会出现 `mythroad/x:/...`。保留既有 `mr_platEx(1204)` 切工作目录语义，不将所有盘符平铺到同一个目录。`Y:`/`Z:` 等控制命令仍由 DSM 切盘层处理。

原 `get_filename` 使用无容量参数的拼接/复制。ESP 分支还需引入带目标容量、能返回失败的路径 helper，并让 open/info/rename 等所有直接调用者检查结果。宿主端的长度检查不能保护已经发生在 DSM 拼接层的越界；UTF-8 路线还需计入转换后的长度膨胀。

下面是完整可独立编译的路径模块 `mrp_path.c`，展示首期明确边界：接受 DSM 已解析相对路径和 A/B/X 盘符，按 GBK 双字节边界解析；不支持 `..` 和其他盘符，遇到时明确报错。该限制必须记入兼容日志，后续如需父目录语义再实现有根边界的组件栈。传入、传出缓冲不能重叠。

**GBK 陷阱：** 双字节字符的尾字节可以是 `0x5C`，不能一律当反斜杠替换。DSM 上游的斜杠规范化也要在 ESP 分支遵守同一字符边界，否则宿主收到的名字已经损坏。以下模块只用于上述 GBK 路线；切换 UTF-8 后必须同步改变字符验证逻辑。

```c
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { INPUT_LIMIT = 512 };

static int path_error(char *out, size_t cap, const char *reason)
{
    if (out != NULL && cap != 0) out[0] = '\0';
    fprintf(stderr, "mrp_path: %s\n", reason);
    errno = EINVAL;
    return -1;
}

static int is_sep(unsigned char ch)
{
    return ch == '/' || ch == '\\';
}

int mrp_vfs_path(const char *input, char *out, size_t cap)
{
    char source[INPUT_LIMIT];
    const char *root = "/mrpfs";
    size_t n = 0;
    if (input == NULL || out == NULL || cap == 0)
        return path_error(out, cap, "invalid argument");
    while (n < sizeof(source) && input[n] != '\0') ++n;
    if (n == 0 || n == sizeof(source))
        return path_error(out, cap, "empty or overlong input");
    memcpy(source, input, n + 1);

    size_t pos = 0;
    if (n >= 2 && source[1] == ':') {
        if (n < 3 || !is_sep((unsigned char)source[2]))
            return path_error(out, cap, "drive path must be absolute");
        switch (source[0]) {
        case 'a': case 'A': root = "/mrpfs/mythroad/disk/a"; break;
        case 'b': case 'B': root = "/mrpfs/mythroad/disk/b"; break;
        case 'x': case 'X': root = "/mrpfs/mythroad/disk/x"; break;
        default: return path_error(out, cap, "unsupported drive");
        }
        pos = 3;
    } else if (is_sep((unsigned char)source[0])) {
        return path_error(out, cap, "physical absolute path forbidden");
    }

    size_t used = strlen(root);
    if (used >= cap) return path_error(out, cap, "output too small");
    memcpy(out, root, used + 1);
    while (pos < n) {
        while (pos < n && is_sep((unsigned char)source[pos])) ++pos;
        size_t first = pos;
        while (pos < n && !is_sep((unsigned char)source[pos])) {
            unsigned char ch = (unsigned char)source[pos];
            if (ch >= 0x81 && ch <= 0xfe) {
                if (pos + 1 >= n)
                    return path_error(out, cap, "truncated GBK character");
                unsigned char trail = (unsigned char)source[pos + 1];
                if (trail < 0x40 || trail == 0x7f || trail > 0xfe)
                    return path_error(out, cap, "invalid GBK character");
                pos += 2;
                continue;
            }
            if (ch >= 0x7f || ch < 0x20 || ch == ':')
                return path_error(out, cap, "invalid path byte");
            ++pos;
        }
        size_t len = pos - first;
        if (len == 0 || (len == 1 && source[first] == '.')) continue;
        if (len == 2 && source[first] == '.' && source[first + 1] == '.')
            return path_error(out, cap, "parent path unsupported");
        if (cap - used < 2 || len > cap - used - 2)
            return path_error(out, cap, "output too small");
        out[used++] = '/';
        memcpy(out + used, source + first, len);
        used += len;
        out[used] = '\0';
    }
    return 0;
}

#ifdef MRP_PATH_TEST
#include <assert.h>
int main(void)
{
    char out[128];
    assert(mrp_vfs_path("mythroad/winmine.mrp", out, sizeof(out)) == 0);
    assert(strcmp(out, "/mrpfs/mythroad/winmine.mrp") == 0);
    assert(mrp_vfs_path("X:\\save.dat", out, sizeof(out)) == 0);
    assert(strcmp(out, "/mrpfs/mythroad/disk/x/save.dat") == 0);
    assert(mrp_vfs_path("a:/./one//two", out, sizeof(out)) == 0);
    assert(strcmp(out, "/mrpfs/mythroad/disk/a/one/two") == 0);
    assert(mrp_vfs_path("x:/\x81\x5c.bin", out, sizeof(out)) == 0);
    assert(strcmp(out, "/mrpfs/mythroad/disk/x/\x81\x5c.bin") == 0);
    assert(mrp_vfs_path("x:/\x81", out, sizeof(out)) == -1);
    assert(mrp_vfs_path("x:/../secret", out, sizeof(out)) == -1);
    assert(out[0] == '\0');
    assert(mrp_vfs_path("/spiffs/a", out, sizeof(out)) == -1);
    assert(mrp_vfs_path("mythroad/x:/a", out, sizeof(out)) == -1);
    assert(mrp_vfs_path("mythroad/a", out, 8) == -1);
    puts("MRP path tests passed");
    return 0;
}
#endif
```

**编码路线比较：**

- 首版 `flags=0`，FatFS API 为 ANSI/OEM + CP936，文件名从 DSM 到宿主保持 GBK；字体资源路径和首测包名用 ASCII。
- 如改为 `FLAG_USE_UTF8_FS` + FatFS UTF-8 API，DSM 会转换输入文件名；但 **USE_FINDDIR 分支没有把枚举结果转回 GBK**，宿主必须补上。见 [dsm.c:401](mythroad/dsm.c#L401)。
- 任一路线都要测试中文目录、中文文件、长名、非法序列；避免双重 GBK/UTF-8 转换。上述路径模块仅规范化路径，不承担字符集转换。

### 3.4 绘图、脏区、RGB565

| 层/函数                                        | 实际责任                                             |
| ---------------------------------------------- | ---------------------------------------------------- |
| VM `_DrawPoint` / 清屏 / 位图绘制            | 保留现有软件绘图，写 `mr_screenBuf`                |
| VM `_DrawText` / `_DrawTextEx`             | GBK 转 UCS-2，布局、换行/裁剪，调用字形绘制          |
| DSM `mr_getCharBitmap` / `mr_platDrawChar` | 当前字体适配；不是函数表新增项                       |
| DSM 表 `getScreenBuffer`                     | 返回提前分配好的 150 KiB PSRAM 屏幕；启动前检查非空  |
| DSM 表 `drawBitmap`                          | 接收刷新请求、裁剪并合并脏区、通知 LCD               |
| LCD 任务                                       | 从整屏按 stride 取矩形，打包、换字节、提交并等待完成 |

**[源码事实]** [mythroad.h:66](mythroad/include/mythroad.h#L66) 的 `MAKERGB` 是 R5:G6:B5，屏幕寻址为 `y*screen_width+x`。[mythroad.c:1554](mythroad/mythroad.c#L1554) 的 `_DispUpEx` 把**整屏基址**传给 `mr_drawBitmap`。因此裁剪到 `(x,y,w,h)` 后，第 r 行源地址是：

```text
framebuffer + (y + r) * 240 + x
```

不能从传入指针开头连续发 `w*h*2` B。应在至少 32 位有符号中间类型中算矩形交集，空交集直接不提交；奇数像素数可扩大脏区到对齐边界以满足 DMA 传输长度要求。

**另一个需单独验证的入口：** [MRF_DispUp:1572](mythroad/mythroad.c#L1572) 传入位图偏移指针，不能无条件套用整屏合同。原式还使用位图高度作行步长。首阶段须用非方形位图测试该入口；若应用使用它，应在 VM 适配处明确源 stride、归一化为屏幕刷新，而不是由宿主猜指针布局。

**字节序与通道序分开处理：**

- 小端内存中的红色 `0xF800` 是 `00 F8`；默认 ST7789 大端像素传输应发送 `F8 00`。
- 首版只在 DMA 条带中写高字节、低字节；不原地交换 VM 屏幕。
- `RGB/BGR` 配置控制通道，不能替代字节交换。
- v5.2.5 的 ST7789 驱动确实支持 `data_endian=LITTLE`，可作为减少交换的后续方案，但必须与发送数据匹配，不能再交换一次。[E-ST7789][E-ST7789]
- `esp_lcd_panel_draw_bitmap` 的右/下坐标是**开区间**。返回只说明已提交，直到 `on_color_trans_done` 才能复用源缓冲。[E-LCD][E-LCD]
- 当前软件屏幕是 RGB565，不证明所有 `.bm/.bmp` 资源都同一布局。资源解码、透明色和翻转另做样本测试。

### 3.5 字库：复用 Unicode 路线

**[源码事实]** [dsm.c:77](mythroad/dsm.c#L77) 打开 `system/gb16.uc2`，按 `ch*32` 定位；ASCII 字宽 8，其余 16，字高 16。[mythroad.c:629](mythroad/mythroad.c#L629) 从 `c2u` 得到 UCS-2 **大端字节串**并逐字处理。

| 项目     | `gb16.uc2`（首选）                | HZK16（替代方案）                         |
| -------- | ----------------------------------- | ----------------------------------------- |
| 索引     | `offset = Unicode_codepoint * 32` | `((lead-0xA1)*94 + trail-0xA1)*32`      |
| 适用字符 | 当前实现的 UCS-2 码位               | 仅有效 GB2312 区位，不能覆盖全部 GBK 扩展 |
| ASCII    | 同一 Unicode 索引，取 8 像素宽      | 通常需要另配 ASCII 字库                   |
| 资源大小 | 本仓库文件为 2,097,152 B            | 取决于实际区位覆盖，不与 UC2 互换         |
| 改动     | 保留现有查字链路                    | 需要重写索引、缺字和布局适配              |

具体实现：

1. Flash 文件系统保留字库，不整文件复制到 PSRAM。
2. 读取恰好 32 B 字形；短读、seek 失败和文件缺失明确报告；启动预检字库长度。
3. 行位图按 `uint8_t` 组合成高字节在前的 16 位掩码；避免历史 `char` 的符号扩展差异。
4. 加 64 字左右的小缓存，减少逐字 seek/read；先测中文菜单，再考虑扩大。
5. 验证“中文、上有名不、ASCII、数字、空格、标点、边界裁剪”；归档源码已留有部分字符转换异常的注释，不能自动归因于 LCD。

`USE_LOAD_TABLES_FROM_FILE` 还要求：

```text
system/tab_gb2ucs_8140_FE4F.dat
system/ucs2gb_4e00_9fa5.dat
```

见 [encode_init:309](mythroad/encode.c#L309)。当前字体资源目录没有这两个文件。**[设计]** 首版不定义该宏，使用 [tables.h](mythroad/include/tables.h) 内置编码表驻 Flash；后续若切文件模式，须生成并核验表结构、字节序和尺寸，不能只烧字库。

### 3.6 定时器、睡眠、时间

| 字段                     | 实现                                                                                |
| ------------------------ | ----------------------------------------------------------------------------------- |
| `timerStart(uint16 t)` | 单个 `esp_timer` one-shot，毫秒转微秒；重启前停止旧计时，更新 generation/deadline |
| `timerStop()`          | 清 armed、递增 generation、停止硬件计时并使旧队列事件失效                           |
| `get_uptime_ms()`      | `(uint32_t)(esp_timer_get_time()/1000)`；按无符号差处理约 49.7 天回绕             |
| `getDatetime()`        | `time()` + `localtime_r`/`gmtime_r`，逐字段填 `mr_datetime`，不能强转结构   |
| `sleep(ms)`            | `vTaskDelay`，非零毫秒向上取到至少一个 tick；不是忙等，不允许在 ISR 调用          |

**定时器竞态设计：** timer 回调在短临界区检查 `armed` 和绝对 `deadline`；未到当前 deadline 的旧回调不投递。到期后锁存当前 generation 并清 armed；VM 消费前再次比对 generation。不能只在旧回调执行时读取“当前 generation”就认为消除了重启竞态。

队列满时，timer 回调记录 pending 到期事件和溢出计数，不阻塞系统 timer task；VM 用有界队列等待定期检查 pending，再消费一次。输入任务保留未成功投递的按下/释放次序，不静默丢释放；无法维持有界积压时进入明确的输入溢出错误状态。`t=0` 的兼容语义单独测试，设计为尽快入队，不能直接递归调用 VM。

`mr_timer()` 已按 one-shot 状态管理；上层脚本/SDK 可自行再次启动。[mythroad.c:4060](mythroad/mythroad.c#L4060) 不支持把同一到期无条件处理多次。ESP timer 官方亦要求回调尽量只投递事件。[E-TIMER][E-TIMER]

未设置实时时钟时，不伪造“当前日期已正确”；通过串口设置或后续 SNTP 初始化，无法提供有效日期则按错误返回并记录。暂停/恢复应调用 VM 原函数；归档恢复可能以 300 ms 重启挂起 timer，并不保存原剩余时长，先保持该行为后测。[mythroad.c:3969](mythroad/mythroad.c#L3969)

### 3.7 网络、声音、震动与 UI：首期完整失败表

**所有槽位填入正确签名的函数，不留 NULL。** 下表不是“假成功”清单：

| 字段                                                              | 首期行为                                     | 后续路线                        |
| ----------------------------------------------------------------- | -------------------------------------------- | ------------------------------- |
| `initNetwork`                                                   | 记录不支持，立即 `MR_FAILED`，不再异步回调 | Wi-Fi 初始化结果经 VM 队列回调  |
| `getHostByName`                                                 | 同上，失败而不是 IP 0                        | lwIP DNS，结果在 VM 任务交付    |
| `mr_closeNetwork`                                               | 不支持返回失败                               | 释放宿主网络状态                |
| `mr_socket`                                                     | 失败，绝不返回有效句柄                       | 独立 socket 句柄表              |
| `mr_connect`                                                    | 失败                                         | 映射非阻塞连接和 `MR_WAITING` |
| `mr_getSocketState`                                             | 失败/规范错误状态                            | 状态机                          |
| `mr_closeSocket`                                                | 失败，记录无效句柄                           | 关闭并使句柄失效                |
| `mr_recv` / `mr_send`                                         | -1，不把失败当 0 B 成功                      | 明确 EOF、短收发、would-block   |
| `mr_recvfrom` / `mr_sendto`                                   | -1，输出地址不当有效结果使用                 | UDP 语义                        |
| `mr_startShake` / `mr_stopShake`                              | 无电机则失败                                 | GPIO/PWM 驱动晶体管/电机驱动器  |
| `mr_playSound` / `mr_stopSound`                               | 无音频服务则失败                             | 音频任务 + 解码/合成 + I2S      |
| `mr_dialogCreate` / `mr_dialogRelease` / `mr_dialogRefresh` | 创建/操作失败，日志列出 API                  | 自绘对话框 + 正确的结果事件     |
| `mr_textCreate` / `mr_textRelease` / `mr_textRefresh`       | 同上                                         | 自绘文本视图                    |
| `mr_editCreate` / `mr_editRelease`                            | 失败                                         | 自绘键盘、长度限制、编辑句柄    |
| `mr_editGetText`                                                | NULL + 日志；调用链必须检查 NULL             | 返回具有明确生命周期的文本      |

**不能只实现 stub 而忽略调用者。** [dsm.c:570](mythroad/dsm.c#L570) 的 UTF-8 编辑转换路径需要 NULL 防护；Lua 包装层也要保持原错误结果形状。首次遇到必需但未实现的 API 时允许应用明确失败，不保证任意应用都能在 stub 下继续运行。

网络包装存在 `MR_WAITING` 时保留回调上下文、立即完成时释放的分支。[dsm.c:777](mythroad/dsm.c#L777) 首期立即失败不能随后再次回调，否则会重复使用已释放的上下文。

I2S 只能输出 PCM，**不能直接“播放 MIDI 字节”**。支持 MIDI 还需事件解析、合成器/音色、混音、计时和额外 RAM/Flash；初期可先做简单 PCM/WAV，再评估 MIDI。用户当前未提供 I2S DAC/功放，因此不提前占用引脚。

### 3.8 不在 DSM 表中的平台函数

这些由 [dsm.c](mythroad/dsm.c) 本地实现，ESP 分支在这里接硬件，不能添加虚构的函数字段：

| API / code                      | ESP 方案                                                                       |
| ------------------------------- | ------------------------------------------------------------------------------ |
| `mr_sendSms`                  | 返回失败并日志；删除 ESP 分支中的旧假成功                                      |
| `mr_call` / `mr_connectWAP` | 原签名为 void，记录不支持；在有结果的上层包装处报告失败，不硬改返回类型        |
| `mr_cacheSync`                | 禁用所有原生装载后可为 no-op；它不能把 ARM 代码变成 Xtensa                     |
| `mr_plat` 1205                | 仅已启用并校准触摸时报告支持                                                   |
| `mr_plat` 1206 / 1218         | 中文能力/卡状态按真实能力回答，不照抄手机环境假值                              |
| `mr_platEx` 1201              | 保留字宽/高数据结构及生命周期                                                  |
| `mr_platEx` 1204              | 保留工作目录/盘符切换                                                          |
| `mr_platEx` 1207              | 保留 UCS-2 → GBK 编码功能                                                     |
| `mr_platEx` 1222 / 1223       | 背光开关接 GPIO7；亮度只能在应用实际请求契约明确后映射 LEDC                    |
| `mr_platEx` 1305              | `esp_vfs_fat_info` 读取实际总/空闲量，按原 16 B 输出合同填充并检查单位、溢出 |
| 1012–1015、1224、1307 等       | 沿用明确“不支持/忽略”语义，不能伪造额外内存                                  |
| 1017 等手机信号功能             | 无网络/蜂窝硬件，不返回虚假信号强度                                            |
| 未知 code                       | 记录 API/code，按原约定 `MR_IGNORE` 或 `MR_FAILED`，输出指针/长度不留垃圾  |

依据 [dsm.c:612](mythroad/dsm.c#L612)、[dsm.c:636](mythroad/dsm.c#L636)。新 ESP 分支必须保留非 ESP 行为，不为本任务顺便改变桌面宿主。

---

## 第四章：fixR9 / EXT 裁剪与兼容策略

### 4.1 它解决的是 ARM 静态数据基址，不是脚本 VM

**[源码事实]** [fixR9.c:1](mythroad/fixR9.c#L1) 和 [genR9R10.js:286](mythroad/asm/genR9R10.js#L286) 所生成的 `r9r10.s` 处理 ARM R9/R10 静态数据基址保存/恢复及跨 EXT 回调。当前工作区没有生成的汇编文件，历史 `.mpr` 保留了它的编译项。加载入口为 EXT 数据加 8，随后注册原生函数与 RW 状态。这套寄存器/指令约定在 Xtensa 上无意义。

只不编译汇编文件还不够：初始化函数表、网络回调、原生 callback 注册仍可能引用 ARM 符号，链接会失败；更不能把 `fixR9` 改成空函数后继续跳转 ARM 字节。

### 4.2 最小侵入裁剪表

**[设计]** 使用 `ESP32_PORT` 分支，非 ESP 原代码保留：

| 位置                                          | 修改                                                                            |
| --------------------------------------------- | ------------------------------------------------------------------------------- |
| 构建清单                                      | 不编译 `fixR9.c`、`asm/r9r10.s`、SDK ARM 入口                               |
| `mythroad.c:153` / `:254`                 | 原生内外函数表及 `asm_*` 初始化仅非 ESP 编译                                  |
| `mythroad_init:5203`                        | 保留普通库/状态初始化，只跳过两个 EXT 表初始化；不把整个函数删掉                |
| `_mr_TestCom` case 2–6，约 `:2897`       | 拒绝由整数注册 event/timer/stop/pause/resume 原生函数指针                       |
| `_mr_c_function_new:3217`                   | 原生函数注册 ESP 返回失败，不写可调用地址                                       |
| `_mr_TestCom1` case 800 / 802，约 `:3447` | 不改写文件头、不 cache-sync、不执行入口；Lua 返回一个失败状态                   |
| `_mr_TestCom1` case 801，约 `:3460`       | 不调用原生 helper；Lua 返回空字符串和失败状态两个值                             |
| `_mr_TestCom1` case 900                     | 不导出原生 port table，明确不支持                                               |
| `dsm.c:777` 网络 EXT 回调分支               | ESP 不引用 `getR9/setR9/fixR9`；首期网络 stub 不意味着可以保留未解析 ARM 符号 |
| 事件入口的 native callback slots              | 保持为空；保留脚本 `dealevent`、timer、suspend/resume 分支                    |

**返回值形状很重要：**

```text
_strCom(800, ...) / _strCom(802, ...):
    mrp_pushnumber(L, MR_FAILED); return 1

_strCom(801, ...):
    mrp_pushstring(L, ""); mrp_pushnumber(L, MR_FAILED); return 2

_com(2..6, ...) 的 Lua 调用:
    返回一个失败状态，不能注册地址

有独立 int32 状态合同的原生 C 入口:
    返回 MR_FAILED
```

以上是**调用点修改规范**，不是独立 C 文件。Lua C 函数的 `return` 是结果个数，不能直接 `return -1`。也不能给所有 `L==NULL` 路径笼统套一个返回约定，须保留对应原生调用者的合同。

不删除 `_strCom(601)` 等普通资源读取，也不删除 `mr_mallocExt/freeExt`；它们并非都属于 ARM 执行。

### 4.3 包级检测与真实兼容率

1. 使用现有容器解析器验证列表、边界、重复条目，再检查 `.ext`（大小写规范化）。
2. 首版采用保守策略：含 EXT 即提示“**包内含原生扩展，本固件暂不支持**”并拒绝。仅有文件存在不证明一定执行，不能把提示写成已完成调用链证明。
3. 对目标包检查脚本 loader 调用、入口和错误分支；重命名的原生资源可能绕过后缀检查。
4. **运行时 loader gate 永远保留**，静态扫描不是最后防线。

**[样本实测]** 当前扫雷脚本确实执行 `_strCom(800, ...)`，随后通过 801 调用，失败走 `Exit()`；不是仅“包里有个不用的 EXT”。详见 [阶段 0](ESP32_PHASE0.md)。因此首阶段验收必须换一个已验证纯脚本包/扫雷版本，或取得其原生部分源码另行移植；不通过删除 EXT 文件伪造纯脚本。

兼容率应报告两项：全部 664 包中可启动比例，以及筛出的无原生依赖子集中通过比例。没有后续实测前，不给百分比预测。

---

## 第五章：字节码加载链路

### 5.1 既有链路与真实头部

**[源码事实]**

```text
mr_start_dsm
  -> _mr_intra_start
  -> mr_L_loadfile
  -> _mr_readFile（容器定位/解压）
  -> mrp_load
  -> 二进制检测
  -> mr_U_undump
  -> LoadHeader
  -> LoadFunction（递归读 Proto）
  -> 受保护的脚本执行
```

依据 [mr_auxlib.c:532](mythroad/src/lib/mr_auxlib.c#L532)、[mr_do.c:438](mythroad/src/mr_do.c#L438)、[mr_undump.c:211](mythroad/src/mr_undump.c#L211)。

**[样本实测]** 扫雷 `start.mr` 解压 4,536 B，前 32 B：

```text
1B 4D 52 50 80 01 0A 00 00 00 40 73 74 61 72 74
2E 6D 72 00 00 00 00 00 00 00 00 04 2A 00 00 00
```

| 偏移         | 内容                                        |
| ------------ | ------------------------------------------- |
| `0x00..03` | `ESC MRP`                                 |
| `0x04`     | `0x80`                                    |
| `0x05`     | 字节序标志 1，小端                          |
| `0x06..09` | 32 位字符串长度 10                          |
| `0x0A..13` | `@start.mr\0`                             |
| `0x14..17` | lineDefined = 0                             |
| `0x18..1B` | nups、numparams、vararg、maxstack；最后为 4 |
| `0x1C..1F` | lineinfo 数量 42                            |

源文件名属于 `LoadFunction` 的正常 source 字段，**不是应手工剥离的一层头或加密壳**。标准 Lua 也序列化函数源名。

### 5.2 原 loader 对 0x80 已有支持

[mr_undump.h:21](mythroad/src/h/mr_undump.h#L21) 定义 `MRP_SIGNATURE="\033MRP"`、`VERSION=0x80`、`VERSION_50=0x50`。loader 拒绝高于 0x80 或低于 0x50 的版本；读字节序后，对大于 0x50 的版本跳过旧版的类型宽度/数字测试字段。[mr_dump.c:140](mythroad/src/mr_dump.c#L140) 默认也输出 0x80。

**结论 [源码事实 + 样本实测]：** 对这个小端、32 位整数数值模型的样本，原生重编译该 Mythroad VM 后，**不需要改魔数、改版本、去掉源文件名或额外解密**。容器取出并解压后直接交给原 loader。

这不是“任意 0x50–0x80 包都兼容”的证明。简化头部省去了宽度自描述，故应在 ESP 构建中静态检查：

```text
sizeof(int) = sizeof(size_t) = sizeof(Instruction) = 4
sizeof(void *) = 4（宿主旧结构/句柄边界也要检查）
有效 mrp_Number = 当前 mr_user_number.h 选择的 int
目标小端；整数运算和 opcode 编码与样本一致
```

PC 64 位原生验证不能直接沿用宿主 `size_t=8` 读取样本；应使用真正匹配的 32 位构建，或在研究工具中明确读固定 32 位字段。不能由 64 位 loader 失败就推断包损坏。

### 5.3 容器偏移不是重复跳过文件名

**[样本实测]** 扫雷头 `0x04=0x184`，实际列表结束为 `0x18C`，即该值加 8。入口压缩流偏移 `0x19D`；文件名/长度记录位于它之前。索引中的 payload offset 已指向 gzip，不能再跳一次文件名前缀。详细字节证据和 SHA 见 [阶段 0](ESP32_PHASE0.md)。

用户的容器概括可用于识别字段，但实际解析应复用已验证的偏移合同。解压工具支持的 gzip/zlib/raw-deflate 不等于归档运行时三个路径都已实测；运行时只为实际出现且尚不支持的封装增加适配，不进行无界、静默反复试解。

固件解包还应约束：索引/长度算术不溢出、读取不越过文件、输出上限、结束标记和截断检查。旧 VM 不是针对恶意字节码的隔离沙箱；首期使用来源可信且验证过的包。

### 5.4 与标准 Lua 5.0 的具体差异

| 项目            | 当前 Mythroad                                                            | 意义                                                                                                  |
| --------------- | ------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------- |
| 签名/版本       | MRP / 0x80                                                               | 不可直接交标准 Lua                                                                                    |
| 数值类型        | [mr_user_number.h:2](mythroad/src/h/mr_user_number.h#L2) 强制 `USE_INT` | 不能按标准 double 常量读取                                                                            |
| 基本指令        | Lua 5.0 派生编码                                                         | 保留原 VM，不换 Lua 5.1/5.4                                                                           |
| 额外指令        | BNOT=35、BAND=36、BOR=37、BXOR=38                                        | [mr_opcodes.h:184](mythroad/src/h/mr_opcodes.h#L184) 和 [mr_vm.c:780](mythroad/src/mr_vm.c#L780) 有实现 |
| 系统库/事件/API | Mythroad 自有                                                            | 标准 Lua 加几项函数不等于兼容环境                                                                     |

标准 Lua 5.0 的版本和 opcode 对照见 [官方 undump 头][L-HEADER] 与 [opcode 表][L-OP]。此前 launcher 样本已发现 BAND，不能把额外指令当作从不使用的扩展。

### 5.5 PC 提取与 luadec 验证方案

已有可执行工具和测试：[mrp_bytecode.py](../../tools/mrp_bytecode.py)、[test_mrp_bytecode.py](../../tests/test_mrp_bytecode.py)。从仓库根目录运行，输出文件必须不存在，避免覆盖旧证据：

```powershell
python tools\mrp_bytecode.py "..\vmrp.github.io-master\public\mrps\winmine.mrp" --output build\esp32-report-check\start.mr
python -m unittest discover -s tests -p test_mrp_bytecode.py -v
```

**重要限制 [源码事实]：** 归档 [luadec.c:73](mythroad/luadec/luadec.c#L73) 是 `luadec(L, package_filename, outputFile)`，内部扫描 MRP 再加载脚本，并非已提供一个可直接执行的 `luadec.exe start.mr`。此外 [print.c:1914](mythroad/luadec/print.c#L1914) 对未处理 opcode 只输出注释；其反编译 switch 没有四条位运算分支。因此：

1. 先在 PC 原生构建匹配的 VM/只解析 harness，使用正向 loader 检查 Proto 树；不执行计费、网络或原生启动路径。
2. 固定 32 位 ABI；打开函数/指令列表输出，对每个 opcode 范围、常量索引、跳转和子 Proto 检查。
3. 将 `luadec` 链入研究 harness，输入包而非虚构 CLI；输出中一旦出现 `unhandled opcode` 即标记不完整。
4. 要得到可读位运算伪源码，需补四条反编译分支并测试；不以反编译“能输出文件”作为语义正确证明。
5. 对标准指令、四条位运算、负整数、嵌套函数、截断 chunk 建小测试；归档 VM 与 ESP VM 比对输出。

第 1–5 项为**待实施实验**，不是本报告已运行的 luadec 结果。阶段 0 的字节级解析和既有六项工具测试已完成。

---

## 第六章：构建、启动与主循环

### 6.1 原生构建清单及修改范围

依据 [mr_vm_mrp.mpr](mythroad/sdk/mr_vm_mrp.mpr) 与 [mr_vm_full.mpr](mythroad/sdk/mr_vm_full.mpr)。以下相对目录均基于 `docs/research/mythroad`，属于拟纳入组件的源码清单，不声称已经链接通过：

```text
VM:
  src/mr_api.c       src/mr_debug.c     src/mr_do.c
  src/mr_dump.c      src/mr_func.c      src/mr_gc.c
  src/mr_mem.c       src/mr_opcodes.c   src/mr_object.c
  src/mr_state.c     src/mr_string.c    src/mr_table.c
  src/mr_tm.c        src/mr_undump.c    src/mr_vm.c
  src/mr_zio.c

标准/平台库:
  src/lib/mr_auxlib.c       src/lib/mr_auxiliar.c
  src/lib/mr_baselib.c      src/lib/mr_tablib.c
  src/lib/mr_strlib.c        src/lib/mr_socket_target.c
  src/lib/mr_tcp_target.c    src/lib/mr_iolib_target.c

运行时与工具:
  mythroad.c  dsm.c          mem.c          encode.c
  mr_graphics.c             mr_inflate.c   mr_unzip.c
  mr_pluto.c                mr_base64.c    md5.c
  string.c    printf.c      other.c        strtol.c    strtoul.c

parser（二选一）:
  src/parser/mr_code.c + mr_lex.c + mr_parser.c
  或 src/parser/mr_noparser.c

不纳入 ESP:
  fixR9.c  asm/r9r10.s  sdk/main.c  sdk/vmrp.c
  mythroad_mini.c（不能与完整运行时同时编译）
  luadec/*（仅 PC 研究使用）
```

首版建议保留 parser，避免某包入口/动态资源含源码时得到 “parser not loaded”；确认目标集全为预编译 chunk 后才选 noparser。旧 `.mpr` 有 `src/mr_noparser.c` 旧路径，当前文件在 `src/parser`。网络即使 stub，也先保留库注册源文件，避免 `dsm_init` 未解析引用。

头文件应保留原有目录组织，而不是平铺到 IDF 的全局 include 搜索路径：

```text
共享 ABI:
  include/vmrp/dsm.h（仓库根下）

归档 include/:
  dsm.h encode.h fixR9.h md5.h mem.h mr.h mr_auxiliar.h
  mr_auxlib.h mr_base64.h mr_conf.h mr_formaketo.h
  mr_graphics.h mr_gzip.h mr_helper.h mr_lib.h
  mr_socket_target.h mr_store.h mr_tcp_target.h
  mrporting.h mythroad.h other.h printf.h string.h tables.h type.h

归档 src/h/:
  mr_api.h mr_code.h mr_debug.h mr_do.h mr_func.h mr_gc.h
  mr_lex.h mr_limits.h mr_mem.h mr_object.h mr_opcodes.h
  mr_parser.h mr_state.h mr_string.h mr_table.h mr_tm.h
  mr_undump.h mr_user_number.h mr_vm.h mr_zio.h
```

这是保留头文件集合；不是要求 IDF 宿主 include 每一项。ARM 专用头的引用应与实现一起条件编译，不作为 ESP 公共接口。

| 文件/宏                                                               | ESP 处理                                                                                                     |
| --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `include/type.h`                                                    | ESP 分支使用 `<stdint.h>/<stddef.h>`，避免重复定义 `size_t/uintptr_t/ptrdiff_t`；保留 MR 别名和 ABI 断言 |
| `mythroad.c`                                                        | 第四章裁剪，检查直接盘符/图形调用点；其余 VM 行为保留                                                        |
| `dsm.c`                                                             | 平台能力、字体错误、路径边界、UI NULL、ARM 回调分支                                                          |
| `mem.c`、VM/GC 主体                                                 | 首版保留分配/回收策略；仅添加受控诊断，不先换分配器                                                          |
| `TARGET_MOD / MTK_MOD`                                              | 首次按原兼容分支构建并审阅分支；不是 ESP 硬件标志，不能全局套给 IDF                                          |
| `DSM_FULL / MR_PLAT_DRAWTEXT / USE_FINDDIR / USE_GET_SCREEN_BUFFER` | 首版保留，组件内保持一致                                                                                     |
| `USE_LOAD_TABLES_FROM_FILE`                                         | 首版关闭，使用静态编码表                                                                                     |
| `ESP32_PORT`                                                        | 仅 Mythroad 与宿主相关组件定义，不扩散至第三方 IDF 代码                                                      |

检查链接 map 和未定义符号：不能残留可达 `asm_*`、`getR9/setR9`、`fixR9`；不能因静态库暂未拉入某对象就声称所有原生路径已裁剪。

### 6.2 完整 VM 入口流程伪代码

以下为拟编写 `vmrp_main.c` 的**完整流程伪代码**，有意不把尚未实现的宿主函数冒充可链接 C 程序：

```text
全局：
    静态 DSM_REQUIRE_FUNCS 表（寿命覆盖 VM）
    host 状态：STOPPED / STARTING / RUNNING / STOPPING / ERROR
    VM 事件队列、帧 mutex、LCD 通知、timer generation/deadline/pending
    PSRAM 屏幕（宿主持有）、DMA 条带（LCD 持有）
    句柄表、字体缓存、资源预检结果、错误/性能记录

app_main：
    检查 PSRAM 实际可用量；失败 -> 明确错误，不启动 VM
    挂载 FatFS/WL，format_if_mount_failed=false
    验证字库、包索引、入口和 EXT 策略；失败 -> 显示/串口报告
    分配屏幕、DMA 条带和内部队列/锁；任一步失败 -> 按已获取资源逆序释放
    创建 esp_timer（ESP_TIMER_TASK，未启动）
    完整填 DSM 表；首版 flags=0；检查必需回调均非 NULL
    初始化 LCD，创建 LCD 任务
    创建 VM 任务，再创建输入任务/启用 GPIO IRQ
    投递带有已复制包名/入口名的 START 命令

GPIO_ISR：
    只通知输入任务，不直接构造 VM 调用，不做去抖等待

输入任务：
    等待通知或采样周期；采集 GPIO/触摸并去抖
    产生顺序一致的按下/释放、校准后触摸事件
    投递到 VM 队列；满时保留待发事件，记录溢出
    无法维持有界积压 -> 明确输入故障，停止继续灌入

timer 回调：
    短临界区核对 armed、当前 deadline
    到期则记录 generation，清 armed
    非阻塞投递 TIMER；满则锁存 pending + 计数
    不调用 mr_timer、不等待 VM、不访问 arena

VM 任务：
    ret = dsm_init(&funcs)
    ret != VMRP_VER -> 错误退出（成功不是 0）
    循环：
        有界等待队列，并检查 pending TIMER / exit 请求
        过期 generation 的 TIMER -> 丢弃并计数
        获取帧 mutex
        根据命令：
            START：
                仅 STOPPED 可进入，恢复初始工作目录，运行资源/目录预检
                调 mr_start_dsm("相对包名", "start.mr 或 start.m", NULL)
                更新 host 状态，记录 loader 错误
            APP_EVENT：RUNNING 时 mr_event(app_code, p0, p1)
            TIMER：通过 generation 检查后 mr_timer()
            PAUSE：mr_pauseApp()
            RESUME：mr_resumeApp()
            STOP：先禁止外部新事件，调用 mr_stop() 一次
            未知命令：日志，不以未知值调用 VM
        检查 VM 错误状态、host exit 请求；返回 0 不一定证明脚本无异常
        释放帧 mutex
        若有脏区 -> 通知 LCD（不持锁等待完成）
        记录事件耗时、队列水位、堆指标
        若需要退出：
            先使 timer generation 失效并停表
            等待所有当前 VM 调用结束、LCD 使用屏幕结束
            关闭字体/文件/迭代器等宿主句柄
            arena 由 mr_stop 的 mem_free 路径释放，禁止第二次释放
            屏幕可保留给宿主菜单；不与 arena 一起误释放
            清队列、回调和 host 状态；重新启动前重新初始化生命周期

LCD 任务：
    等待刷新通知（可合并）
    获取帧 mutex，取出合并后的脏矩形
    按整屏 stride 打包一条带，完成字节序转换
    提交 esp_lcd，等 DMA 完成后才复用该条带
    全部成功后清已处理脏区；失败记录 LCD 故障，不伪称刷新成功
    释放帧 mutex
```

**[源码事实]** `mr_event` 和 `mr_timer` 内部可能记录脚本错误但仍走通用成功返回，因此验收要看错误状态/错误日志，不能只看返回值。停止代码中实际 timer stop 被注释，且资源释放受宏影响，见 [mythroad.c:3860](mythroad/mythroad.c#L3860)；宿主必须有明确 stop barrier。

### 6.3 外层 DSM 消息与应用事件不能混用

| 外层消息         | 数值 | 原生转发                                             |
| ---------------- | ---: | ---------------------------------------------------- |
| `DSM_INIT`     | -100 | `dsm_init(&funcs)`，返回版本号                     |
| `MR_START_DSM` |  -99 | `mr_start_dsm(filename, script_entry, entry)`      |
| `MR_PAUSEAPP`  |  -98 | `mr_pauseApp()`                                    |
| `MR_RESUMEAPP` |  -97 | `mr_resumeApp()`                                   |
| `MR_TIMER`     |  -96 | `mr_timer()`，**不是 `mr_event(-96,...)`** |
| `MR_EVENT`     |  -95 | 取内部事件结构，再调 `mr_event(code,p0,p1)`        |

依据 [dsm.h:15](../../include/vmrp/dsm.h#L15) 与 [sdk/main.c](mythroad/sdk/main.c)。原生程序可直接使用类型化命令，不必重现把指针塞入整数的 ARM 消息传参。

应用事件见 [mrporting.h:83](mythroad/include/mrporting.h#L83)：按下 0、释放 1、触摸按下 2、抬起 3、移动 12。方向键码为 12–15；物理 A/B 应按应用映射到 SELECT、数字 5/0 或软键等，不能因为枚举中有 A/B=24/25 就假定所有老应用都认识。

`mr_start_dsm` 的第二个参数变量名虽然叫 `ext`，在该启动路径里用于脚本入口选择，不应误当成要求加载 ARM `.ext`。包路径使用相对名；若实际只有 `start.m`，预检后明确传它。

### 6.4 Flash 分区、资源布局和烧录

**[设计]** 16 MiB、无 OTA 的首版完整分区 CSV：

```csv
# Name, Type, SubType, Offset, Size, Flags
nvs, data, nvs, 0x9000, 0x6000,
phy_init, data, phy, 0xF000, 0x1000,
factory, app, factory, 0x10000, 0x300000,
storage, data, fat, 0x310000, 0xCF0000,
```

`0x310000 + 0xCF0000 = 0x1000000`，恰好 16 MiB。应用预算 3 MiB 是预留，不是已测镜像大小；用 `idf.py size-components` 复核。storage 为 12.9375 MiB，扣 2 MiB 字库后最多约 10.9375 MiB，再扣 FAT/WL/存档/预留空间。

**664 是测试集合，不代表可同时安装。** 用户未给包总容量；超出 Flash 时用 SD 卡或按需安装，不承诺压进 16 MiB。

```text
fs/
  mythroad/
    system/
      gb16.uc2
    选定且通过纯脚本检查的包.mrp
    disk/
      a/
      b/
      x/
```

FatFS 镜像在组件 CMake 中调用 `fatfs_create_spiflash_image(storage <fs目录> FLASH_IN_PROJECT)`；它生成带 wear levelling 的镜像，匹配 `esp_vfs_fat_spiflash_mount_rw_wl`。不要用 raw FAT 镜像去匹配 RW/WL 挂载。[E-FAT][E-FAT]

后续固件工程就绪后，在其 ESP-IDF PowerShell 环境执行：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py size-components
idf.py -p COM5 flash monitor
```

`COM5` 替换为实际端口；`FLASH_IN_PROJECT` 使资源镜像随 `flash` 一起烧录，不需要虚构 `make_flash` 内置目标。**重烧资源分区会覆盖存档，操作前备份。**

SPIFFS 备选：CSV 最后一行 subtype 改 `spiffs`，使用 `spiffs_create_partition_image(storage <fs目录> FLASH_IN_PROJECT)` 和 `/spiffs` 挂载；同时必须实现第三章的逻辑目录层。容量应按官方约 75% 可靠使用范围规划，而非使用到最后一字节。[E-SPIFFS][E-SPIFFS]

### 6.5 可独立使用的 LCD / PSRAM 硬件探针

下面四个完整文件组成独立测试工程；不含 MRP，不依赖归档头文件。用于阶段 1 把屏幕方向、色序、DMA 生命周期与 VM 问题分开。**API 已对照 v5.2.5；未在本机交叉编译或烧板。**

目录：

```text
lcd_probe/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    CMakeLists.txt
    lcd_probe.c
```

根 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mrp_lcd_probe)
```

`sdkconfig.defaults`（仅适用于确认使用 Octal PSRAM 的板）：

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
CONFIG_SPIRAM_MEMTEST=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set
# CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY is not set
CONFIG_FREERTOS_HZ=1000
```

`main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "lcd_probe.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_timer heap)
```

`main/lcd_probe.c`：

```c
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { WIDTH = 240, HEIGHT = 320, ROWS = 16, BACKLIGHT = 7 };
static const char *TAG = "lcd_probe";

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    BaseType_t wake = pdFALSE;
    BaseType_t ok = xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    configASSERT(ok == pdTRUE);
    return wake == pdTRUE;
}

void app_main(void)
{
    uint16_t *frame = heap_caps_malloc(WIDTH * HEIGHT * sizeof(*frame),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *stripe = heap_caps_malloc(WIDTH * ROWS * 2,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(frame && stripe && done ? ESP_OK : ESP_ERR_NO_MEM);

    const gpio_config_t bl = {
        .pin_bit_mask = 1ULL << BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&bl));
    ESP_ERROR_CHECK(gpio_set_level(BACKLIGHT, 0));

    const spi_bus_config_t bus = {
        .mosi_io_num = 11, .miso_io_num = -1, .sclk_io_num = 12,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * ROWS * 2
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = 10, .dc_gpio_num = 9,
        .spi_mode = 0, .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 1, .lcd_cmd_bits = 8, .lcd_param_bits = 8
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = color_done
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &callbacks, done));
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 8,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_ERROR_CHECK(gpio_set_level(BACKLIGHT, 1));

    const uint16_t colors[6] = {0xf800, 0x07e0, 0x001f, 0xffff, 0, 0x8410};
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            frame[y * WIDTH + x] = colors[x / 40];

    for (;;) {
        int64_t start = esp_timer_get_time();
        for (int y = 0; y < HEIGHT; y += ROWS) {
            for (int row = 0; row < ROWS; ++row) {
                for (int x = 0; x < WIDTH; ++x) {
                    uint16_t pixel = frame[(y + row) * WIDTH + x];
                    size_t i = ((size_t)row * WIDTH + x) * 2;
                    stripe[i] = (uint8_t)(pixel >> 8);
                    stripe[i + 1] = (uint8_t)pixel;
                }
            }
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
                panel, 0, y, WIDTH, y + ROWS, stripe));
            if (xSemaphoreTake(done, pdMS_TO_TICKS(2000)) != pdTRUE) {
                ESP_LOGE(TAG, "DMA completion timeout");
                ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
            }
        }
        ESP_LOGI(TAG, "frame including packing: %" PRId64 " us",
                 esp_timer_get_time() - start);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

探针只用一个 DMA 条带并严格等待完成；完整运行时可在证明流水线收益后用双条带。所有资源按整次固件运行持有，不是每帧重新申请。默认 20 MHz、RGB、大端、无反色、零偏移；屏模块若要求 BGR、反色或偏移，以色条实测调整，不能同时随意换多个变量。

---

## 第七章：实施计划、风险与性能估算

### 7.1 阶段与验收门槛

| 阶段         | 交付                                                     | 可检查的验收                                                                                   |
| ------------ | -------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| 0：样本/头部 | 已有提取工具与阶段 0 报告；另选纯脚本样本                | 固定 SHA，入口/解压大小一致；无必需原生调用；不把现有 EXT 扫雷误纳入                           |
| 1A：硬件基础 | IDF 工程、PSRAM/LCD 探针、资源镜像                       | PSRAM 内存测试通过；20/40 MHz 色条与边界正确；刷新无超时；缺资源不自动格式化                   |
| 1B：原生 VM  | 源码组件、ABI 断言、全 DSM 表、EXT gate                  | Xtensa 编译/链接成功；无 ARM 引用；最小脚本的整数/位运算/字体/文件/计时器结果与 PC 一致        |
| 2：首个应用  | 确认纯脚本的扫雷版本或替代包                             | 标题与棋盘正确；按键翻开/插旗；连续运行 30 分钟无崩溃、卡键、单调泄漏；暂停/恢复/退出/重启有效 |
| 3：扩展      | 2048 → xxfk → zmdzz → 约 200 KiB 魔塔，均先做依赖预检 | 每款记录必需 API、实际峰值、GC/事件延时、有效帧率和已知缺项；不以“包大小小”推断低堆占用      |
| 4：性能/兼容 | 脏区、字形缓存、快照/条带优化，按需补 API                | 与优化前同输入序列对比；画面/逻辑不变，p95/max 延迟下降；最后再讨论网络/音频                   |

每次测试记录包 SHA、固件提交/源码版本、IDF 版本、板号、PSRAM 模式、CPU/SPI 时钟、arena 大小、峰值与错误 API。兼容失败分成：原生依赖、loader、API 缺失、资源/编码、内存、性能，不混成一个“打不开”。

### 7.2 风险矩阵

分值为**设计阶段主观排序**，概率 P 和影响 I 各 1–5，不是统计概率；同分按阻塞程度排列。

| 风险                               |                   P×I | 规避与降级                                                                   |
| ---------------------------------- | ---------------------: | ---------------------------------------------------------------------------- |
| 入口是脚本但实际依赖 EXT           |                5×5=25 | 先分类；现有扫雷已证明此风险；换纯脚本样本，不绕过 loader gate               |
| 归档源码/类型/ARM 残留不能原生链接 |                4×5=20 | 首先跑 ABI/链接最小构建；私有 include；保留原源码分支                        |
| 不支持的 API 被当成有效结果使用    |                4×4=16 | 全签名 stub + 调用者 NULL/状态防护；记录所需 API，再有序补齐                 |
| 帧缓冲并发、stride、DMA 复用错误   |                4×4=16 | VM 调用级帧锁、窄矩形测试、完成回调；先串行条带                              |
| PSRAM 带宽和逐像素绘制卡顿         |                4×3=12 | 行级操作、脏区、字形缓存；降帧率，不牺牲输入次序                             |
| GC/碎片/realloc 峰值               |                3×4=12 | 测 nblocks 与 arena 两套指标；先 2 MiB，再按样本升 4 MiB；避免无依据调大阈值 |
| timer 重启竞态、队列溢出、重复到期 |                3×4=12 | generation + deadline + pending，队列压力测试，单 VM 线程                    |
| 字库缺失/GBK 表错误/字节序         |                4×3=12 | 资源启动预检、Unicode 索引、中文和色条分别测试                               |
| SPIFFS 目录与长写停顿              | 4×3=12（若选 SPIFFS） | 优先 FatFS；否则实现逻辑目录并保留空间，降低写入频率                         |
| 长脚本阻塞、任务看门狗             |                3×4=12 | VM 安全点统计指令/时间；允许调度而不重入 VM；超预算报错，不直接关闭 WDT      |
| 文件句柄/退出生命周期泄漏          |                3×4=12 | 关闭表、timer stop barrier、100 次启动/退出试验、峰值回归                    |
| 字节码模型不匹配                   |                2×5=10 | 0x80 loader 已有支持；实际风险是类型/新变体，做类型断言与多个 chunk          |
| 16 MiB 容量不足装全集              |                 3×3=9 | 统计安装集大小，留存档余量；SD/按需安装                                      |
| 掉电损坏/自动格式化丢存档          |                 2×4=8 | 禁止挂载失败自动格式化；受控保存/备份；断电实验                              |

执行预算不应在任意 C 调用中直接强杀 VM task。可在 VM 指令调度安全点检查预算，受保护地报告错误；长 C 图形/解包操作也需单独测量。只有硬件看门狗作为最后恢复手段时才允许整机重启，并记录原因。

### 7.3 刷新时间与小游戏帧率

全屏有效载荷为 `240×320×2 = 153600 B`：

| SPI 时钟 | 纯载荷时间 | 理论全屏上限 |
| -------- | ---------: | -----------: |
| 20 MHz   |   61.44 ms |    16.28 FPS |
| 40 MHz   |   30.72 ms |    32.55 FPS |
| 80 MHz   |   15.36 ms |    65.10 FPS |

**[估算]** 尚不含命令、条带间隙、调度、PSRAM 读/换字节、VM 绘制和 GC。80 MHz 仅作算术对照，必须得到屏控制器时序、模块走线及实际测试支持，不能当默认可用频率。

首版单帧锁、串行打包输出下：

```text
T_frame >= T_script_and_draw + T_pack + T_SPI + T_command_and_schedule
```

40 MHz 若脚本/绘图/打包共 5/15/30 ms，不计其他开销时上限约为 **28.0 / 21.9 / 16.5 FPS**。因此对轻量小游戏可以把 **15–25 FPS 全屏动画**作为待验证目标，不是保证；扫雷和 2048 主要事件驱动，更应关注按键到可见反馈延迟。

一个 32×32 RGB565 脏块在 40 MHz 纯载荷约 `32×32×2×8/40e6 = 0.4096 ms`。但很多小事务的命令开销、脏区合并造成的扩大都要实测。优化按 CPU 绘制、SPI 利用率和 p95/max 延迟分别归因，不只报平均 FPS。

---

## 第八章：待验证清单与建议顺序

### 8.1 已由源码/既有实验回答，不再当作未知

| 问题             | 当前结论                             | 仍需怎样验收                             |
| ---------------- | ------------------------------------ | ---------------------------------------- |
| 0x80 检查        | 原 loader 已支持                     | Xtensa 构建后载入同 SHA 的 chunk         |
| `@start.mr`    | 正常 Proto source 字段               | 嵌套函数/不同 source 长度的解析测试      |
| flags 的含义     | 已定义 bit 0 FS、bit 1 EDIT          | 两种编码路线分别端到端测试               |
| `mem_get` 次数 | arena 初始化时一次索取，内部自行细分 | 启停次数与 malloc/free 配对、失败注入    |
| 当前扫雷纯脚本性 | 已证明该版本依赖 ARM EXT             | 换样本或取得原生源码；不重复争论文件后缀 |
| 当前屏幕色值     | 源码是 RGB565                        | 资源解码格式和真实屏通道/字节序仍待测    |

### 8.2 可执行验证任务

| ID / 优先级 | 待验证点                        | 方法与通过标准                                                                                          |
| ----------- | ------------------------------- | ------------------------------------------------------------------------------------------------------- |
| V01 / P0    | 实际板型、PSRAM、电源和屏模块   | 用户提供板号/模块丝印/引脚图；IDF 启动日志确认实际 PSRAM 大小/模式；接线逐项核对第一章                  |
| V02 / P0    | 可作首测的纯脚本应用            | 对候选 blockbreak/life/2048 等先用现有工具解析，再静态检查入口 loader；选定 SHA；不得假定它们一定无 EXT |
| V03 / P0    | 归档 C 的 32 位原生可构建性     | Xtensa 最小组件构建、类型断言、map/nm 检查；无 ARM 符号、无指针截断/类型冲突                            |
| V04 / P0    | 原生 VM 与样本数值/opcode 相容  | PC 匹配 ABI harness 只加载 chunk；四条位运算和负整数小程序与 ESP 对比；截断文件明确失败                 |
| V05 / P0    | LCD 颜色、方向、stride、DMA     | 运行探针色条；再测非零 x/y、窄矩形、奇宽、高宽不等位图；20/40 MHz 无超时/越界/花屏                      |
| V06 / P0    | 字库与编码链                    | PC 读取 UC2 指定码位生成点阵对照；ESP 显示中英文；确认字库长度、缺字行为，记录“上有名不”              |
| V07 / P0    | DSM 路径和文件语义              | 相对包名、切 A/B/X 盘、直接盘符、中文长名、find、EOF、seek、短写、满盘测试；失败状态严格符合表格        |
| V08 / P0    | timer / 事件 / 输入             | start-stop-start、高频重启、队列满、暂停恢复、按键释放、触摸校准；过期 generation 不执行，无重复到期    |
| V09 / P1    | 稳定退出和再次启动              | 连续 100 次启动/退出；文件/目录句柄归零，无迟到 timer 访问旧 arena，无重复 free                         |
| V10 / P1    | 2 MiB 与 4 MiB arena 的 GC/碎片 | 同输入序列记录峰值、最大空闲块、nblocks、GC p95/max、realloc 失败；不能只比较剩余总量                   |
| V11 / P1    | 实际资源压缩/像素格式           | 对目标包逐资源确认 gzip/zlib/raw、位图 stride/透明色/方向；提取器和原 loader 结果对照                   |
| V12 / P1    | 不支持 API 的失败传播           | 强制触发每种网络/UI/声音 stub；无 NULL 调用/假句柄；脚本得到错误而非崩溃；不执行计费行为                |
| V13 / P1    | luadec 的输出可靠性             | 建 PC harness；补/验证 BNOT/BAND/BOR/BXOR；遇到未处理 opcode 即失败标记；不把反编译结果当唯一真值       |
| V14 / P1    | 性能、CPU 预算和看门狗          | 分离 VM/GC/打包/SPI 时间；30 分钟交互和长脚本测试；记录栈高水位，不以关 WDT 通过                        |
| V15 / P2    | 存储容量/掉电/资源更新          | 统计安装集容量；存档写入中断电实验；损坏明确报告；资源重烧前备份并验证恢复                              |
| V16 / P2    | 664 包兼容矩阵                  | 按原生依赖/API/内存/格式分组批量跑，保留失败证据；只对实际通过项声明兼容                                |

**建议下一步：先 V01–V04，再进入宿主编码；不是先堆出一套 ESP 工程后才发现验收包必须运行 ARM。**

### 8.3 报告自身的验证范围

- 阶段 0 的六项工具测试、真实扫雷解压数据已有记录，见 [阶段 0 报告](ESP32_PHASE0.md)。
- 本报告的路径模块已用 PC GCC 的 `-std=c11 -Wall -Wextra -Werror -DMRP_PATH_TEST` 编译运行通过：4 个有效路径、5 个拒绝路径，覆盖盘符、容量、GBK 尾字节 `0x5C` 等。它不验证 FatFS 或 DSM 集成。
- 本次检查了 65 个内联文件链接及其中行号边界，并核算帧大小、arena 加屏幕预算、分区末地址和 SPI 载荷时间。
- 代码示例按 IDF v5.2.5 官方 API 核对；没有 ESP 工具链/目标板的部分均保留“待编译/实机验证”。
- 接线、2/4 MiB arena、任务优先级、15–25 FPS 是设计/估算，不是测试结果。
- 包、字库及历史代码的分发许可需在发布固件/资源镜像前单独确认；本地持有文件不等于获准重新分发全集。

---

## 参考资料

本地源码引用对应当前工作区，后续修改会使行号移动。关键证据入口：

- [共享 DSM 函数表与消息](../../include/vmrp/dsm.h#L15)
- [DSM 路径/字体/平台实现](mythroad/dsm.c#L77)
- [历史宿主的 320 KiB arena](mythroad/sdk/vmrp.c#L157)
- [Mythroad 分配器](mythroad/mem.c#L28)
- [同步 GC](mythroad/src/mr_gc.c#L477)
- [字节码 header loader](mythroad/src/mr_undump.c#L211)
- [原生装载入口](mythroad/mythroad.c#L3447)
- [既有扫雷字节级实验](ESP32_PHASE0.md)

[E-PSRAM]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-guides/external-ram.html
[E-SPI]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/peripherals/spi_master.html
[E-LCD]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/peripherals/lcd.html
[E-ST7789]: https://github.com/espressif/esp-idf/blob/v5.2.5/components/esp_lcd/src/esp_lcd_panel_st7789.c
[E-TIMER]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/system/esp_timer.html
[E-GPIO]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/peripherals/gpio.html
[E-BOARD]: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
[E-SPIFFS]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/storage/spiffs.html
[E-FAT]: https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-reference/storage/fatfs.html
[E-KPSRAM]: https://github.com/espressif/esp-idf/blob/v5.2.5/components/esp_psram/esp32s3/Kconfig.spiram
[E-KCOMMON]: https://github.com/espressif/esp-idf/blob/v5.2.5/components/esp_psram/Kconfig.spiram.common
[E-KFAT]: https://github.com/espressif/esp-idf/blob/v5.2.5/components/fatfs/Kconfig
[L-HEADER]: https://www.lua.org/source/5.0/lundump.h.html
[L-OP]: https://www.lua.org/source/5.0/lopcodes.h.html
