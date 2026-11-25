# 项目背景与历史

## 项目来源

mrpx 源自 [VMRP 原仓库](https://github.com/vmrp/vmrp)。原 README 记载原作者为曾明；VMRP 面向 Mythroad MRP 应用模拟，早期文档、设计思路和实现请以原仓库资料为准。

mrpx 延续其 ARM Guest 执行与 Mythroad DSM 桥接方向，并整理了现代仓库结构、桌面/Web 构建流程、测试和发布文档。项目名称、发行包和网页运行文件使用 `mrpx`；为兼容保留的内部标识包括 `include/vmrp`、`VMRP` 编译宏和部分运行时环境变量。

## 维护者说明与致谢

本仓库维护者在项目说明中注明：本项目基于原项目并借助 AI 进行大规模重构整理，原有实现不作为维护者的原创成果。原项目作者与贡献者的工作应归属于其各自作者；使用和分发请遵循项目许可证及相关第三方许可证。

原仓库及其文档是理解项目早期背景的重要参考。本仓库的说明重点放在当前工程、可复现构建和研究资料边界上。

## 历史 Guest 资料

仓库保留了 Mythroad Guest 运行时、MRC SDK、ARM/Thumb 实验和 MRP/EXT/FMT 样本，统一放在 [`docs/research/`](research/README.md)。这些文件主要用于研究，不是当前桌面或 Web 构建的自动输入。

当前宿主直接加载的预编译 Guest 模块和 MRP 种子列在 [`assets/runtime/manifest.json`](../assets/runtime/manifest.json)。历史 Guest 源码与这些现有二进制之间，尚未建立完整、可重复的生成和对应关系；修改历史源码不会自动改变实际运行的 Guest。

固定的 ARM/Thumb 回归包单独保存在 [`tests/fixtures/mrp/`](../tests/fixtures/mrp/README.md)，其哈希和用途有记录。研究时应保留原始样本，使用独立的新样本验证格式或 ABI 假设。
