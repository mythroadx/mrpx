# Mythroad Guest 研究源码

本目录原位于仓库根 `mythroad`，保存历史 ARM Guest 运行时及 SDK 工程，不参与当前 Windows、Linux 或 WASM 宿主构建。研究流程见[研究总览](../README.md)。

## 代码地图

| 文件 / 目录 | 研究内容 |
| --- | --- |
| [mythroad.c](mythroad.c)、[mythroad_mini.c](mythroad_mini.c) | 完整版与精简版运行时、事件和资源管理 |
| [dsm.c](dsm.c) | Guest 平台适配及宿主函数表调用 |
| [src/](src/) | Lua 风格的脚本虚拟机、字节码、GC、解析器及标准库 |
| [include/](include/) | 历史 SDK 类型、接口与编码表 |
| [asm/](asm/) 与 [fixR9.c](fixR9.c) | ARM 寄存器上下文、R9/R10 切换及生成脚本 |
| [luadec/](luadec/) | 脚本反编译实现 |
| [sdk/](sdk/) | 原 `build` 目录中的 `.mpr` 工程、入口源码和 Sky SDK 批处理脚本；不是构建产物 |
| [Makefile](Makefile) | 未标准化的历史 ARM/桌面构建实验 |
| [unused/](unused/) 与 [src/lib/unused/](src/lib/unused/) | 未启用的绑定、脚本和标准库参考实现；不是可直接删除的垃圾 |

根目录其余 C 文件包括压缩解包、编码、图形、内存和基础函数实现，同样保留供研究使用。此次核对没有发现可删除的目标文件、可执行文件或缓存；不能仅凭 `unused`、`build` 目录名删除源码。

## 与当前工程的边界

- 当前运行依赖是 [assets/runtime/cfunction.ext](../../../assets/runtime/cfunction.ext)，不是现场编译本目录的结果。历史源码与现有二进制的对应关系尚未证明。
- 唯一由当前宿主直接使用的 DSM 头文件已移到 [include/vmrp/dsm.h](../../../include/vmrp/dsm.h)，声明、字段顺序与 ABI 保持不变。[归档入口](include/dsm.h)仅转发到此共享定义，避免维护两份接口。
- Guest 编译须将本目录的 `include` 加入头文件搜索路径，以提供历史 `mrporting.h`；归档 Makefile 已补齐该路径，SDK `.mpr` 本来就包含它。宿主仍通过 `VMRP` 使用固定宽度类型，不引入历史 SDK 类型。
- `sdk` 改名没有改变工程文件到其兄弟源码目录的相对层级。批处理仍含历史 SDK 绝对路径，不是现代构建入口，也不要未经验证直接运行。
- 历史 Makefile 仍有缺失的输入，例如 `asm/r9r10.s` 及与实际目录不符的 parser 路径；保存它是为了研究，不代表已支持构建。先确定 SDK、生成步骤、链接布局和许可证，再建立新的可复现流程。

## 研究与仓库卫生

先对照 DSM 函数表、R9/R10 切换和 Guest 装载布局，再研究完整/精简运行时与反编译器。保留原始许可证、作者说明及必要的参考源码。

新增编译输出、SDK 下载、报告和截图统一放在仓库根忽略的 `build` 目录，不写回研究源码；历史批处理需要先调整输出位置。不要随意替换运行种子或回归输入，任何有意更新都必须经过 Guest 边界和真实 MRP 回归验证。

## 历史低地址内存映射实验

宿主曾保留一段永久禁用的实验：分配并清零 `[0, CODE_ADDRESS)`，再以 `UC_PROT_ALL` 映射到 Guest 地址 0。旧注释推测部分游戏会读写该区间，但没有记录样本、可重复步骤或验收结果；它从未参与当前构建，现从宿主源码移除并在此记录。

当前 Guest 有效区间仍为 `[START_ADDRESS, END_ADDRESS)`，不新增低地址映射。后续研究必须先记录触发样本和指令、界定所需范围及权限、设计内存释放和宿主地址转换，再通过 Guest 边界与真实 MRP 回归；不能把旧实验当成已验证的兼容修复。
