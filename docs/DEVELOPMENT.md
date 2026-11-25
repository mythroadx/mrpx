# 开发环境与复现指南

本文详述 **Windows x64 + PowerShell 7** 参考环境；**Linux 原生桌面与 WASM** 请按 [Linux 构建指南](LINUX.md) 操作。除特别说明外，命令都在仓库根目录执行。原生界面分别使用 Win32/GTK4，网络分别使用 Winsock/POSIX；macOS 尚未支持。

## 1. 参考版本与工具职责

| 依赖 | 参考版本 | 用途 / 约束 |
| --- | --- | --- |
| Git | 可用的 Git for Windows | 获取仓库及固定版本后端 |
| PowerShell | 7.3+；本机验证 7.6.9 | 执行 `.ps1`；不是 Windows PowerShell 5.1 |
| MinGW-w64 GCC | 13.2.0，`x86_64-w64-mingw32` | 桌面 x64；不是 MSVC 或 ARM 交叉编译器 |
| GNU Make | 4.4.1 | 根目录统一构建入口 |
| MSYS2 工具 | `sh`、`mkdir`、`cp`、`rm` | Make recipe 使用 POSIX 命令；仅有 `mingw32-make` 不够 |
| SDL2 MinGW SDK | 2.32.10，x86_64 | 桌面头文件、导入库及运行 DLL |
| Unicorn 桌面库 | 官方 2.1.4，Windows x64 | 参考验证使用官方 wheel 中的 DLL |
| Emscripten SDK | **6.0.12** | WASM 编译；不要用会漂移的 `latest` 复现 |
| 原生 Windows CMake | 已验证 4.3.1-msvc1 | WASM 配置；不能使用 MSYS `usr\bin\cmake.exe` |
| Ninja | 已验证 1.13.0.git.kitware.jobserver-pipe-1 | WASM 后端构建生成器 |
| Node.js / npm | 控件单测 18+；音频准备/测试与 WASM 参考 SDK 自带 24.19.0 | MIDI 资源使用 `npm ci` 按锁文件安装；桌面构建不依赖 npm |
| Python | 3.10+；本机回归验证 3.15.2 | 桌面 MRP 回归、本机 HTTP 服务；使用标准库 |
| Capstone | 5.x，可选 | 仅 `make debug`、`make de`；不在最小复现链内 |

上表区分工具最低要求与已测版本，不保证所有版本组合兼容。首次下载依赖、Emscripten SDL2/zlib ports、WASM 后端源码及 MIDI 音色需要联网；准备完成后，测试与网页运行不需要第三方网络。现有 Guest 模块、MRP 样例和字体随仓库提供，宿主构建不需要 Sky SDK/ARM 编译器。

建议检出到 `C:\dev\mrpx` 一类短路径。WASM 当前补丁已处理带空格源码路径；**官方桌面 Unicorn 源码构建**仍有 QEMU 路径限制。以下 DLL 路线无需构建该源码。

## 2. 系统准备及目录约定

先安装 Git for Windows、PowerShell 7、Python、MinGW-w64 x64、GNU Make/MSYS2 基础工具、原生 Windows CMake 和 Ninja。可以通过各工具的官方安装包或已有包管理器安装；不要把本机的 MSYS GCC 当作 MinGW-w64 GCC。

在当前 PowerShell 会话中让 MinGW-w64 的 `bin` 位于 MSYS2 `usr\bin` 之前。例如，按实际安装位置调整：

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:PATH
Get-Command gcc, make, sh, cmake, ninja, python, pwsh | Select-Object Name, Source
gcc -dumpmachine
gcc -dumpfullversion
make --version
```

`gcc -dumpmachine` 必须为 `x86_64-w64-mingw32`。确保可调用 `tar`、`cp`、`rm` 和 `mkdir`。WASM 不用 MinGW GCC，但根 Makefile 仍需要 POSIX recipe 工具。不要为了修复 `emcc` 而全局替换系统 PATH。

建议布局：

```text
C:\dev\
  emsdk\                         # 独立 SDK，不提交
  mrpx\
    config.example.mk            # 共享配置模板
    config.local.mk              # 本机配置，不提交
    deps\                        # 桌面依赖及下载文件，不提交
      downloads\
      unicorn\include\unicorn\
      unicorn\lib\unicorn.dll
      SDL2-2.32.10\x86_64-w64-mingw32\
    assets\runtime\              # 共享只读 Guest 依赖及 manifest，提交
    build\                       # 全部忽略；不提交下面的任何文件
      web\                       # 完整发布站点：页面、JS/WASM、runtime、vendor
      unicorn-wasm\              # 固定后端源码、头文件、静态库和缓存
    bin\                         # 桌面程序、cfunction.ext 和 mythroad 运行副本；整体忽略
    wasm\
      config.mk                  # WASM 编译设置，提交
      web\                       # HTML 和辅助 JS 源码，提交
```

```powershell
git clone <本仓库克隆URL> mrpx
Set-Location .\mrpx
Copy-Item .\config.example.mk .\config.local.mk
New-Item -ItemType Directory -Force .\deps\downloads | Out-Null
```

将克隆 URL 替换为当前 mrpx 仓库地址，不要误将原 VMRP 仓库当成已经重构的版本。本地已有配置时不要再次覆盖。模板采用本指南的 DLL/SDL2 解压布局；不创建本地配置时，Make 默认读取 `deps/unicorn/build` 下的源码构建静态库和同一 SDL2 目录。依赖未准备完成仍会明确构建失败，不会自动下载或回退到旧目录。旧本机 `windows/SDL2-2.32.10` 布局已统一到 `deps/SDL2-2.32.10/x86_64-w64-mingw32`，自定义配置和 PATH 也应按实际位置更新。

配置文件使用 GNU Make 的 `/` 路径写法，变量值**不包引号**；recipe 在调用工具时加引号。PowerShell 命令仍使用 Windows `\`。命令行覆盖优先，例如 `make main 'UNICORN_ROOT=C:/deps/unicorn' 'UNICORN_LIB=C:/deps/unicorn/lib/unicorn.dll'`。切换头文件、库或编译选项后，必须重新构建对应对象，不能拿旧 `.o` 证明新配置可用。

### 本地配置速查

| 变量 | 含义 |
| --- | --- |
| `CC` / `ARCH_FLAGS` | 桌面编译器 / 默认 `-m64`；不能改变 Guest 32 位 ABI |
| `UNICORN_ROOT` | 桌面 Unicorn 根目录，下面应有 `include\unicorn` |
| `UNICORN_BUILD_DIR` / `UNICORN_LIB` | 桌面库目录 / 具体静态库或 DLL |
| `SDL2_ROOT` | SDL2 x64 MinGW SDK 根目录；不是归档最外层目录 |
| `CAPSTONE_ROOT` / `CAPSTONE_INCLUDE` / `CAPSTONE_LIB` | 可选调试/反汇编依赖 |
| `PYTHON` / `NODE` | 测试命令，默认 `python` / `node` |
| `EMCC` / `WASM_CMAKE` | WASM 宿主编译器 / 后端使用的原生 CMake |
| `WASM_UNICORN_ROOT` / `WASM_UNICORN_BUILD` | WASM 固定源码目录 / 三个静态库的输出目录 |
| `BUILD_DIR` | 桌面对象和测试目录，默认 `build`；不改变 WASM 后端缓存或 `bin` 输出位置 |
| `DEBUG=1` | 启用 Capstone，生成 `bin\main-debug.exe` |

`EMSDK`、`EM_CACHE`、`PATH` 是进程环境变量，应在 PowerShell/SDK 激活脚本中设置，不要把 PowerShell 语句写进 `.mk`。`VMRP_RUNTIME_DIR` 可覆盖默认程序同目录（源码构建为 `bin`）的桌面资源根目录，`VMRP_DISABLE_NETWORK=1` 禁止桌面网络；不要将可写目录设为 `assets/runtime`。默认资源及交互输入行为见[项目介绍](PROJECT.md#使用方法)。

VS Code 的 C/C++ 配置与默认 `deps` 布局、GNU C11 和编译宏对齐；Linux 系统头文件路径对应已验证的 Ubuntu 24.04 x64 `pkg-config --cflags sdl2 gtk4` 输出。编辑器配置不会自动读取 `config.local.mk`：使用自定义 SDK 路径或其他发行版时，应同步调整本机编辑器配置，不能把 IntelliSense 结果当作真实构建验收。

## 3. 桌面最小复现

### 3.1 获取固定依赖

参考 DLL 来自 [PyPI Unicorn 2.1.4](https://pypi.org/project/unicorn/2.1.4/#files) 的 `unicorn-2.1.4-cp37-abi3-win_amd64.whl`。这里仅解压 C 头文件和 DLL，**不安装或导入 Python 扩展**，wheel 的 Python 标签不约束运行 MRP 回归的 Python 版本。

```powershell
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$wheel = '.\deps\downloads\unicorn-2.1.4-cp37-abi3-win_amd64.whl'
Invoke-WebRequest 'https://files.pythonhosted.org/packages/70/3d/de7be9bd1addabe6d8a1369381f8a080400c349850e978689c5e18287957/unicorn-2.1.4-cp37-abi3-win_amd64.whl' -OutFile $wheel
if ((Get-FileHash $wheel -Algorithm SHA256).Hash -ne 'd7107500c64ce5c168fbff6bef9485b5db1350050036f4cea568650cf8bdbdf5') {
    throw 'Unicorn wheel checksum mismatch'
}
python -m zipfile -e $wheel .\deps

$sdl = '.\deps\downloads\SDL2-devel-2.32.10-mingw.tar.gz'
Invoke-WebRequest 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz' -OutFile $sdl
if ((Get-FileHash $sdl -Algorithm SHA256).Hash -ne '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2') {
    throw 'SDL2 SDK checksum mismatch'
}
tar -xzf $sdl -C .\deps
```

上述下载 URL/校验值来自发布方的版本元数据。不要把 SDL3、SDL2 的 VC 开发包或 `i686-w64-mingw32` 子目录混入此配置。`SDL2_ROOT` 应包含 `include\SDL2`、`lib`、`bin` 三个目录。

### 3.2 构建、运行与验收

运行依赖从 SDK/工具链获取，不预置在项目 `bin`。加载 DLL 的搜索路径必须包含 Unicorn 的 `lib`、SDL2 SDK 的 `bin` 和所用 MinGW 编译器的 `bin`（安装工具链时应已加入）；修改 PATH 只对当前终端及其子进程生效：

```powershell
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$env:PATH = (Join-Path $PWD 'deps\unicorn\lib') + ';' + (Join-Path $PWD 'deps\SDL2-2.32.10\x86_64-w64-mingw32\bin') + ';' + $env:PATH
make main
make test test-controls test-capture test-web-controls test-mrp
.\bin\main.exe
```

参考环境使用官方 Unicorn 2.1.4 DLL，已验证六组 C 测试、Windows 控件测试、Web 控件/加载器测试、八项回归工具测试和六个真实桌面 MRP 场景。Guest 边界测试还验证了从文件重载 ARM 代码后会刷新 Unicorn 翻译缓存；显示测试覆盖四方向旋转、鼠标/方向键映射及局部刷新，控件测试覆盖旋转时按键的配对释放，以及 Windows Console 的日志批量追加、清空和超长消息。`test-capture` 使用系统 WIC 和 Media Foundation 编码并解码图片和视频，检查像素、静止画面、旋转补黑边及时长，并验证按钮和退出时自动保存。该测试需要 Windows 的媒体组件和 H.264 编码器，不需要 FFmpeg。Windows Guest 日志在事件循环中批量更新界面，避免密集日志逐条重绘阻塞游戏；不改变游戏时钟或定时器间隔。C 测试会故意触发无效地址、队列失败等错误日志；判断是否通过应看测试完成标记及进程退出码，而不是要求输出中没有 `ERROR`。

仓库不携带 SDL2、Unicorn 或 MinGW 运行 DLL；旧 `bin` 内容已移除，整个目录只在构建时作为产物目录生成。Windows 测试按需从 SDL2 SDK 复制 DLL 到忽略的 `build/tests`，其他动态依赖通过上述 PATH 查找。部署桌面程序时应从对应 SDK/工具链收集所需运行库，不能从旧仓库文件凑齐。不要把不同架构的 DLL 复制到系统目录。图形控件测试会短暂打开窗口，不能在无交互桌面的环境中假定它可运行。

`make main` 自动按 `assets/runtime/manifest.json` 将种子刷新到程序所在的 `bin`，仅覆盖同名种子，不删除额外应用数据；单独刷新可执行 `make prepare-runtime`，不需要 npm。程序默认启动 `dsm_gm.mrp` 应用列表，将自己的 MRP 放到 `bin/mythroad`，再从应用列表选择，或用 Open MRP、Ctrl+O 或拖放打开。运行时不再修改源码种子。MRP 自动回归使用全新临时资源、SDL dummy 和禁网设置；结果保存在 `build\mrp-regression\run-*\results.json`，每例还有日志和 BMP。不会自动更新像素基线。日常 GUI 运行产生的 `bin\mythroad\c`、`mrcc` 是应用数据，全部忽略，不应当提交或复制到基线资源中；旧版 `build/runtime` 等目录中的现有本机数据不会自动迁移或删除，请备份后自行迁移。

### 3.3 可选：源码依赖及调试

希望完全源码构建桌面依赖时，使用官方 [Unicorn](https://github.com/unicorn-engine/unicorn) 的 `2.1.4` 标签、ARM target 和同一套 MinGW-w64 x64 编译器。将 `UNICORN_ROOT`、`UNICORN_BUILD_DIR`、`UNICORN_LIB` 改为对应头文件及静态库；不要用 WASM 的 TCI 库替换桌面库。源码路径应无空格，完整复现步骤见本指南[桌面最小复现](DEVELOPMENT.md#3-桌面最小复现)。

Capstone 5.x 同样使用 MinGW-w64 x64 静态库，配置 `CAPSTONE_ROOT` 或 `CAPSTONE_INCLUDE` / `CAPSTONE_LIB`，再执行 `make debug`、`make de`。这些是可选路径，不属于本文已验证的最小 DLL 复现链；默认不要求安装它们。

## 4. WASM 最小复现

### 4.1 安装并激活固定 SDK

SDK 可以放在仓库同级；如果本机已安装 **6.0.12**，无需重复安装：

```powershell
git clone https://github.com/emscripten-core/emsdk.git ..\emsdk
& '..\emsdk\emsdk.bat' install 6.0.12
& '..\emsdk\emsdk.bat' activate 6.0.12
& '..\emsdk\emsdk_env.ps1'
emcc --version
node --version
```

不要在 PowerShell 输入 `call emsdk_env.bat`。`.bat` 的环境设置不会留在父 PowerShell，使用 `.ps1`；每个新终端（包括 VS Code 新任务进程）都要重新激活。SDK 激活后可能切换 Node/Python，记录**激活后**的实际版本。

### 4.2 固定后端与构建

根 [Makefile](../Makefile) 是统一用户入口，运行 `make help` 查看命令。[scripts/build.ps1](../scripts/build.ps1) 保留为跨平台实现层；PowerShell 7.3+ 是共同要求，MIDI 打包仍使用锁定的 npm/esbuild 依赖，不将实现硬塞进 Make。

| Make 命令 | 职责 |
| --- | --- |
| `help` | 列出入口与关键参数；默认不指定目标仍构建 main |
| `native-deps JOBS=4` | 显式获取固定官方 Unicorn / Windows SDL2 并构建原生 SDK |
| `web-deps` | 按锁文件执行 npm ci，不编译宿主 |
| `prepare-runtime RUNTIME_DIR=<路径>` | 根据 manifest 复制种子，保留目标应用数据 |
| `prepare-web-audio` | 复用下载缓存、打包 ESM、复制 worklet 和许可证；须先 web-deps |
| `prepare-web` | 准备 HTML/CSS/JS 和 Web 运行种子；须先准备音频 |
| `wasm-unicorn JOBS=4` | 固定后端、补丁、编译、smoke；须先激活 SDK |
| `main` / `debug` / `de` / `wasm` | 增量编译相应目标；wasm 须先准备音频、后端并激活 SDK |
| `package VERSION=vX.Y.Z PLATFORM=wasm` | 打包已构建产物，生成发行 ZIP；详见[发布指南](RELEASING.md) |
| `test-go` / `test-package` / `test-build` | Go HTTP、ZIP 与 Make/PowerShell 入口测试 |
| `serve PORT=18766` / `test-browser BROWSER_URL=<URL>` | 本地 HTTP 服务 / 真实 MRP 与音频测试；使用两个终端 |

Make 参数可放在 `config.local.mk` 或命令行：`native-deps` 支持 `NATIVE_CMAKE`、`JOBS`、`UNICORN_ROOT`、`SDL2_ROOT`；`wasm-unicorn` 支持 `WASM_CMAKE`、`JOBS`、`WASM_UNICORN_ROOT`、`WASM_UNICORN_BUILD`；`package` 使用 `VERSION`、`PLATFORM` 和原生 SDK 路径。`PLATFORM` 默认当前原生平台，`VERSION` 必填且严格校验。`RUNTIME_DIR` 只控制种子复制位置，非默认运行位置仍需设置 `VMRP_RUNTIME_DIR`。

从仓库根目录执行 Make；带空格的路径作为完整参数传入，例如 `"NATIVE_CMAKE=C:/Program Files/CMake/bin/cmake.exe"`。路径和发布参数经环境传给 PowerShell，不作为 shell 代码拼接。原脚本子命令保留兼容，使用限制仍由实现层校验。Make 不安装系统工具，也不自动下载编译依赖或启动浏览器服务；只有显式准备命令获取 SDK/npm/音频资源，失败会终止执行。原生 SDK/下载缓存在忽略的 `deps`，WASM 源码、音频缓存及发行输出在忽略的 `build`。

在 `config.local.mk` 中取消 `WASM_CMAKE` 注释并填写原生 Windows CMake 的位置（若 PATH 首项已经正确则不必）。例如：

```makefile
WASM_CMAKE = C:/Program Files/CMake/bin/cmake.exe
```

部分 CMake 版本会触发 Emscripten 的共享库支持警告。本工程后端显式设置 `BUILD_SHARED_LIBS=OFF`，当前静态库构建不使用该功能；不要因此关闭其他构建警告或修改 SDK。

然后在**已激活 SDK 的同一 PowerShell**中：

```powershell
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$env:EM_CACHE = Join-Path $env:LOCALAPPDATA 'mrpx\emscripten-6.0.12\mrpx-cache'
make web-deps
make prepare-web-audio
make wasm-unicorn
make wasm test-wasm-unicorn test-web-controls test-web-audio
```

`make wasm-unicorn` 调用 [PowerShell 实现层](../scripts/build.ps1) 的 `unicorn` 子命令，克隆 [elliott-wen/unicorn.wasm](https://github.com/elliott-wen/unicorn.wasm) 固定提交 **`4ce0fc93249d0f581ed22049a36baa547ef70447`** 并应用 [仓库补丁](../scripts/unicorn-wasm-2.1.4.patch)。补丁覆盖官方回调 ABI、wasm32 ARM 配置、配置失败退出码、WASM guard-page 限制和编译警告。脚本检查提交/补丁，重复执行不会重复打补丁，不会自动覆盖不匹配的本地修改。

这不是官方原生 JIT 直接编译 WASM。旧 `wasm/unicorn` 已移除，不再携带不兼容的旧头文件/静态库。当前工程需要配套的新头文件及 `libunicorn.a`、`libarm-softmmu.a`、`libunicorn-common.a` 三个库，禁止混用。源码和库目录由 `WASM_UNICORN_ROOT` / `WASM_UNICORN_BUILD` 指定，根 Makefile 会将它们传给脚本；下载内容和产物均位于忽略目录。

`make wasm` 除编译外，也调用 `make prepare-web`：将 `wasm/web` 的 HTML/CSS/JS 及 manifest 中的种子复制到 `build/web`。发布只需完整提供该目录，不能直接打开源码 HTML。修改前端后重新执行 `make wasm` 或 `make prepare-web`；只改前端不会触发不必要的 C 重编译。后者需要已准备的 MIDI 资源，但不需要激活编译器。工作台布局支持桌面/窄屏，画布始终保持 240×320 CSS 像素；不要通过 border、padding 或缩放画布改变 SDL 输入坐标。

后端编译、smoke 链接使用 `build\unicorn-wasm` 下的 `engine-cache`、`link-cache`；宿主使用 `%LOCALAPPDATA%\mrpx\emscripten-6.0.12\mrpx-cache`，避免把首次解压的 SDL2/zlib ports 放在同步目录。本机验证时，仓库内新缓存曾在目录重命名时报 `WinError 5`，改用上述独立本地缓存后首次完整编译成功；未据此断定文件占用的具体来源。

首次构建会下载、解压 ports 并编译 libc 等系统库，可能需要数分钟。Windows 长路径/8.3 短路径可能触发 Emscripten sanity 缓存重建，不应并发编译到同一个 EM_CACHE。脚本退出会恢复它自己改变的 PATH/EM_CACHE；外层终端设置的 SDK 环境仍由调用者管理。

### 4.3 启动浏览器并验证

```powershell
make serve PORT=18766
```

| 地址（前缀 `http://127.0.0.1:18766`） | 预期 |
| --- | --- |
| `/build/web/main.html` | 中文 launcher，21 键面板、可见 Console |
| `/build/web/main.html?f=../../tests/fixtures/mrp/mpc.mrp&name=mpc.mrp` | 本机 MPC 测试夹具（不随包发行）；按 5 打开编辑框，提交中文后再打开可读回 |
| `/build/unicorn-wasm/smoke.html` | `VMRP_UNICORN_WASM_OK` |
| `/tests/wasm_mrp_browser_tests.html` | 标题 `PASS: mrpx WASM regression`，七项 `passed: true` |
| `/tests/web_audio_browser_tests.html` | 点击 Run 后标题 `PASS: mrpx MIDI AudioWorklet`，输出 PCM 峰值 |

浏览器七项回归包含六个真实像素/输入场景，以及 MPC 编辑框中文提交、Guest 读回和取消保留原文；没有用 mock WASM 替代。页面必须保持运行至完成，慢机器/后台标签页节流可能触发超时。Node smoke 另有 20 秒子进程硬超时。浏览器测试依赖 localhost 安全上下文下的 Web Crypto，不应双击 `file:` 页面。

该 HTTP 服务只监听本机，并暴露仓库根目录，适合本地测试，不是生产服务器或公网部署方案。完成后在对应终端 Ctrl+C 停止。浏览器文件系统只在内存中，刷新会重置数据；修改源码后应同步更新 HTML/JS/WASM 并清理旧站点缓存。

### 4.4 本地 MIDI 资源与分发

旧 `ScriptProcessorNode` 播放器替换为 SpessaSynth AudioWorklet，不提供旧接口回退。准备步骤需要 Node.js 24+ 与 npm；桌面构建不受影响。

- `npm ci` 使用提交的 `package-lock.json`：`spessasynth_lib` **4.4.10**、`spessasynth_core` **4.4.12**、打包工具 `esbuild` **0.28.2**，传递依赖也由锁文件固定。
- `make prepare-web-audio` / `npm run prepare:web-audio` 打包本地 ESM、复制 worklet 与许可证，从作者仓库下载 **GeneralUser GS 2.0.3** SoundFont（32,319,396 字节）。
- 音色固定于 `mrbumpy409/GeneralUser-GS` 提交 `684543d5e5efaef08d02be50dcda8d552478fa60`，SHA-256 为 `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe`。下载缓存在 `build/web-audio-downloads`，重复准备验证哈希并复用缓存；损坏时报错，不会静默使用。
- 生成的 `build/web/vendor/spessasynth` 不提交，但部署时须**完整分发**，包括 SoundFont、worklet、模块及全部许可证。网页只读取同源资源，不访问 CDN；`make wasm` 缺资源会提示准备命令，不自动联网安装。
- SpessaSynth/core 使用 Apache-2.0；GeneralUser GS 使用独立自定义许可证。准备脚本保留作者原文及其历史采样来源说明，商用分发前应审阅，不能把音色当作 Apache-2.0 资产。其余打包依赖的许可证同样随资源复制。

页面必须位于 HTTPS 或 localhost，浏览器须支持 AudioWorklet。先点击 **启用 / 恢复声音**；不能绕过浏览器用户激活要求。启用前仅保留最新一首待播 MIDI；停止会取消待播并静音。`mr_playSound` 同步返回 0 表示请求已接受，异步加载/解析失败会显示在音频状态或 Console，不代表已出声；失败后可重新启用并重发播放请求。WAV/MP3 未在本次扩展。

`make test-web-audio` 的六项测试覆盖生命周期与竞态；真实浏览器测试另行量测有限 PCM 样本：播放/循环峰值至少 `0.001`，单次播放自然结束/停止后峰值不超过 `0.000001`。这验证的是特定 MIDI 的实际音频输出，不代表所有 MIDI 文件、音色或移动浏览器均兼容。

## 5. 验收顺序与产物

| 阶段 | 命令 / 检查 | 产物 |
| --- | --- | --- |
| 桌面编译 | `make main` | `bin\main.exe`，`build\release\obj` |
| 桌面单测 | `make test test-controls` | `build\tests` |
| 构建入口 | `make test-build` | 资源保留、路径边界、页面与种子统一准备、损坏音色拒绝及下载清理；需要 Node，临时夹具自动清理 |
| JS 逻辑 | `make test-web-controls` | 终端测试结果（七项） |
| MIDI 资源 | `make web-deps` 后 `make prepare-web-audio` | `build\web\vendor\spessasynth`（含许可证） |
| MIDI 逻辑 | `make test-web-audio` | 终端测试结果（六项） |
| MIDI 真实音频 | 打开音频测试页并点击 Run | 单次结束、循环与停止的 PCM 峰值 |
| 桌面真实 MRP | `make test-mrp` | 八项工具单测、六场景及独立 `run-*` 诊断 |
| WASM 依赖 | `make wasm-unicorn` | 固定源码、三库、smoke JS/WASM/HTML |
| WASM 编译和发布准备 | `make wasm` | 完整 `build\web` 站点（含 JS/WASM、runtime、vendor） |
| 后端执行 | `make test-wasm-unicorn` | 退出码 0 和 smoke 成功标记 |
| 浏览器端到端 | 打开上述回归页 | 七场景 PASS |

只运行 Web mock 测试不等于验证真实 WASM，只看到页面不等于 MRP 已成功启动。`make clean` 删除已知对象/测试目录、`build/web`、`build/unicorn-wasm` 与音频下载缓存，包括依赖源码和静态库；需重新执行 `make web-deps`（若尚未安装依赖）、`make prepare-web-audio` 与 `make wasm-unicorn`，且务必先保存后端源码的本地修改。它不再递归删除整个 `build`：`bin/mythroad` 和旧版 `build/runtime` 的本机应用数据、其他自定义诊断目录、`deps`、本机配置及 LOCALAPPDATA 宿主缓存均保留。清理运行数据需自行备份后针对具体目录操作。

## 6. 常见故障与边界

| 现象 | 检查 / 处理 |
| --- | --- |
| 缺少 `SDL2.dll` / `unicorn.dll`，程序打不开或异常退出 | 检查 x64 SDK、DLL 搜索 PATH 和复制步骤；不要关闭错误检查 |
| `sh`、`cp`、`rm` 不存在 | 安装 MSYS2 基础工具并为当前终端配置 PATH |
| CMake 将 `C:\...` 拼成 `/cwd/C:/...` | 误用了 MSYS CMake；配置 `WASM_CMAKE` 为原生版本 |
| ports 解压/重命名时报 `WinError 5` | 先停止使用该缓存的构建，检查具体路径的占用/权限；使用上文 LOCALAPPDATA 独立缓存后重试。不修改 SDK 脚本，不禁用安全软件，不把失败当成功 |
| SDK 提示 shared libraries 不受当前 CMake 支持 | SDK 6.0.12 对 CMake 4.2.0–4.3.3 的已知提示；本项目只构建静态库，已验证不影响。未屏蔽；需要去除此提示时选择 SDK 提示范围之外的原生版本并重新验证 |
| `saveSetjmp` / `testSetjmp` 未定义 | 正在链接旧 WASM 库；按新脚本重建，不能关闭未定义符号检查 |
| `null function or function signature mismatch` | 核对固定后端、补丁及头/库是否配套；不要伪造 ABI shim |
| 页面还是旧行为，`name` 参数不生效 | 清除站点缓存/禁用开发者工具缓存，再刷新页面；确认 JS/WASM 都已重新生成 |
| MRP 像素不同但能运行 | 对比资源、Guest 包名和真实输入事件；不能直接改基线。MPC 底部时钟已排除，launcher 的 Web 资源集与桌面不同 |
| Console 输入按钮禁用 | 只有 Guest 调用 `mr_editCreate` 才能输入；它不是 stdin、REPL 或宿主命令行 |
| MIDI 无声或资源 404 | 点击启用声音；确认 HTTPS/localhost、AudioWorklet 支持、`make web-deps` / `make prepare-web-audio` 已完成且整个 vendor 目录已部署，查看音频状态及 Console |
| Web 网络功能不工作 | 原生 socket 接口未在 Web 实现；真实网络、所有 MRP/移动浏览器均不在当前回归保证内，MIDI 只验证上文的具体音频场景 |

报告问题时附上项目 `git rev-parse HEAD`、未提交修改说明、失败命令/退出码、相关日志和版本信息；不要提交本机绝对路径、密钥或应用私有数据。可在 **SDK 激活后**采集：

```powershell
git rev-parse HEAD
git status --short
gcc -dumpmachine
gcc -dumpfullversion
make --version
pwsh --version
& 'C:\Program Files\CMake\bin\cmake.exe' --version  # 换为 WASM_CMAKE 的实际值
ninja --version
emcc --version
node --version
python --version
Get-Command gcc, make, sh, cmake, ninja, emcc, node, python | Select-Object Name, Source
```

提交可复现改动时应包含 `wasm/web` 源码、`assets/runtime/manifest.json` 及列出的预编译 Guest 输入、构建配置、补丁、相关测试与文档。`cfunction.ext` 是刻意保留的 ARM 运行依赖，不是宿主构建输出；新增种子须先更新 manifest，网页和桌面仅复制清单内的文件。不要提交 `config.local.mk`、`deps`、`build`、生成的 JS/WASM 或本机 Guest 数据。先前未提交的工程重组/源码修改也必须一并交付，否则其他人仅检出旧提交不能复现当前工作区。

源码树不保存 `.o/.obj` 目标文件、Visual Studio 索引/用户状态（`.ncb/.suo/.user`、`.vs`）、调试缓存或 Python 字节码；这些均由根忽略规则排除。MRC 研究源码及具名的历史 EXT/FMT 样本归档到 `docs/research/mrc`，两个自动回归 MRP 单独位于 `tests/fixtures/mrp`；原研究目录不再放行目标文件。研究目录、待验证结论及工作顺序见[研究指南](research/README.md)。不要仅凭“非 C 源码”删除 SDK 工程配置、源码归档、锁文件、运行 DLL 或 Guest 输入；`windows` SDK、`node_modules` 及已准备的 `build/web` 仍有实际用途，只是不应提交。
