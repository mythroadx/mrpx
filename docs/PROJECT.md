# 当前项目介绍

## 项目定位

mrpx 是基于原 VMRP 项目整理的 Mythroad MRP 模拟器。它在 x86-64 桌面宿主中通过 Unicorn 执行 ARM Guest 代码，并桥接文件、网络、图形、输入和计时器等 Mythroad DSM 接口。

桌面版本支持 Windows 和 Linux；WebAssembly 版本可在现代浏览器运行。ARM Guest ABI 保持 32 位，宿主构建目标为 x86-64。项目目前不是 ESP32 移植版，也不承诺兼容所有 MRP。

## 主要功能

- 使用 Unicorn 执行 ARM/Thumb Guest 指令，并通过 Mythroad DSM 函数表桥接宿主能力。
- SDL2 提供桌面显示和输入；Windows 使用 Win32 控件，Linux 使用 GTK4 控件。
- WebAssembly 版本提供浏览器画面、手机键盘、Console 和 MRP 文件打开能力。
- 支持真实 MRP 回归样例、Guest 内存边界测试及可选 Capstone 指令跟踪。

## 使用方法

桌面版默认启动 `dsm_gm.mrp` 应用列表。将 MRP 放入可执行文件旁的 `mythroad` 文件夹后，从列表选择；也可以通过 **Open MRP**、Ctrl+O 或拖放打开。完整的安装、构建和运行指引见[开发与研究指南](GUIDE.md)。

桌面虚拟键盘的方向键以 ↑、↓、←、→ 图标显示；◧、◨ 为左右软键，☎ 为接听，☎× 为挂机。**Rotate 90°** 按钮每次将画面顺时针旋转 90°，四次恢复原方向，不增加旋转快捷键。横屏时窗口尺寸随之调整，鼠标位置、虚拟方向键及键盘方向键/WASD 跟随画面；数字键和软键保持原始键值。旋转仅改变显示与输入映射，不改变游戏的逻辑分辨率。

Windows 虚拟键盘上的 **Photo** 保存 PNG 截图；**Record** 开始录制，再按 **Stop Rec** 结束并保存无声 H.264 MP4。只捕获游戏画面，文件自动保存到运行目录的 `captures` 文件夹，不覆盖已有文件；退出或切换游戏时也会自动保存。录制按实际时间计时，静止画面会重复；录制期间旋转时视频保持开始时的尺寸，画面等比缩放并补黑边。截图始终采用当前显示方向。录像依赖系统的 Windows Media Foundation 和 H.264 编码器，某些 Windows N 系统可能需要安装媒体功能包；不可用时会显示错误。Linux 和 Web 版本暂不提供这两个按钮。

WebAssembly 页面需经 HTTP(S) 或 localhost 打开，不能直接双击 HTML。部署和本机浏览器测试步骤见[开发环境指南](DEVELOPMENT.md#43-启动浏览器并验证)。

## 工程结构

| 路径 | 用途 |
| --- | --- |
| `src/`、`include/` | 桌面宿主、DSM 桥接、平台接口 |
| `wasm/` | WebAssembly 构建配置与网页源码 |
| `assets/runtime/` | 桌面和 Web 共用的 Guest 运行时及资源种子 |
| `tests/fixtures/mrp/` | 固定 ARM/Thumb MRP 回归样例 |
| `tools/`、`scripts/` | MRP 分析、测试及构建实现 |
| `docs/research/` | 历史 Mythroad/MRC 源码、SDK 和格式研究样本 |

## 兼容性与安全边界

不同 MRP 可能包含脚本字节码、ARM/Thumb 原生扩展或两者兼有。宿主的内存范围和 ABI 检查用于避免 Guest 越界访问宿主缓冲区，但不构成完整沙箱；运行来源不明的 MRP 仍需谨慎。

当前桌面与 Web 回归覆盖固定的启动器、MPC 测试夹具及 ARM/Thumb 样例，不代表所有设备、网络服务、音频功能或应用均兼容。项目支持范围、验证方法及限制详见[指南](GUIDE.md)。
