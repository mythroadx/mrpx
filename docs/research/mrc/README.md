# MRC 研究源码

本目录原位于仓库根 `mrc`，用于研究 MRP C 应用、包格式和底层 ABI，不参与当前桌面/WASM 主程序编译。研究顺序和验证要求见[研究总览](../README.md)。

## 代码地图

| 文件 / 目录 | 研究内容 |
| --- | --- |
| [mrc_base.h](mrc_base.h) | 历史 MRP C SDK 的类型、入口、事件及平台接口声明 |
| [mrp.h](mrp.h)、[mrp.c](mrp.c) | MRP 头部、索引、元信息和打包实现 |
| [mr_table_offsets.c](mr_table_offsets.c) | 宿主函数表字段、大小和映射偏移实验 |
| [mr_c_function_offsets.c](mr_c_function_offsets.c) | C 模块状态结构及桥接映射实验 |
| [mrc_extChunk_offsets.c](mrc_extChunk_offsets.c) | EXT chunk 结构及桥接映射实验 |
| [asm/](asm/README.md) | ARM/Thumb 示例源码、原始归档和 EXT/FMT 格式样本 |
| [baseLib/](baseLib/) | 基础类型、函数地址和库函数相对位置实验 |

当前回归输入单独保存于 [tests/fixtures/mrp](../../../tests/fixtures/mrp/README.md)。研究样本的归档位置不代表已经建立新的编译或打包流程。

## 阅读和修改边界

- ABI 实验对照当前 [bridge.c](../../../src/bridge.c) 和 [dsm.h](../../../include/vmrp/dsm.h)，不要将历史接口表直接覆盖宿主实现。
- 这里保留原始 SDK 声明，未统一为现代固定宽度类型。研究输出必须注明目标数据模型；特别注意 `long`、指针和结构对齐。
- 打包代码依赖 `mrc_exb.h` 等外部历史 SDK 内容，当前目录不提供一个独立、完整的现代打包器。
- 基础库目录含旧 VS 工程和 SDK 构建配置，不保证它们在当前编译器直接可用。迁移保留内部目录关系，未升级这些工程。
- 原始源码中的许可证、作者说明及样本归档应保留；新的编译输出、研究报告和截图只写入忽略的 `build`。