# 固定 MRP 回归输入

这些文件是测试输入，不是可清理的编译产物。它们从原 `mrc/asm` 迁入，包名及字节保持不变。

| 包 | 场景 | 完整文件 SHA-256 |
| --- | --- | --- |
| [asm.mrp](asm.mrp) | ARM EXT、大小端、定点运算及右软键事件 | `226f8ea41d3a89a2176feb20ca11d5084fff9e8aa2ddaaa46a64ecb8789ad03f` |
| [asm_thumb.mrp](asm_thumb.mrp) | Thumb EXT、计算及触摸坐标事件 | `5438ffbeb522dcb09bd726fcae3825edb0f98430fb9343b8b4dabaf781dda24f` |

文件哈希用于确认输入身份，不是像素基线。桌面和 Web 的像素基线分别维护在 [mrp_regression.py](../../../tools/mrp_regression.py) 和 [浏览器回归](../../wasm_mrp_browser_tests.html)，不会自动更新。

历史源码、原始 SDK 归档和 EXT/FMT 分析资料位于 [docs/research/mrc/asm](../../../docs/research/mrc/asm/README.md)，尚未建立重新生成这两个包的可复现证明。不要在原包名下直接重编或修改固定输入；新增研究实验使用新包名，并说明来源、预期行为及工具链。

测试时仍使用 `asm.mrp` / `asm_thumb.mrp` 作为 Guest 可见包名。桌面 runner 复制它们到临时 Guest 文件系统；Web runner 通过相对发布页面的 `../../tests/fixtures/mrp/...` URL 加载，不需要加入日常运行种子的 manifest。
