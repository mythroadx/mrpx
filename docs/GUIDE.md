# 研究与开发指南

根据目标选择相应指南：

| 目标 | 指南 |
| --- | --- |
| 安装依赖、桌面构建、回归及浏览器运行 | [Windows 开发环境与复现指南](DEVELOPMENT.md) |
| Linux 原生桌面与 WebAssembly 构建 | [Linux 构建指南](LINUX.md) |
| MRP、Guest ABI、ARM/Thumb 和历史源码研究 | [MRP 研究指南](research/README.md) |
| 打包 ZIP、版本标签及 GitHub Actions 发布 | [发布指南](RELEASING.md) |

从仓库根目录开始，先查看 `make help`，再按对应平台指南准备工具链。常见本地验证命令：

```powershell
make test test-build test-mrp
```

完整桌面构建、真实浏览器和 WASM 回归需按平台指南先准备相应 SDK 和依赖。不要把忽略目录下的构建输出、Guest 运行数据或外部 MRP 当成源码输入。

## 研究原则

- 先区分宿主实现、历史研究源码和预编译 Guest 二进制；不要假定它们能够一一对应。
- 记录样本来源、SHA-256、工具链、输入和观察结果；将未证实推断明确标记为假设。
- 新实验使用独立样本，不覆盖固定回归输入；改动桥接或 ABI 后运行 Guest 边界和实际 MRP 回归。
- 新生成的报告、截图、二进制和临时 SDK 放在忽略的 `build/` 下。
