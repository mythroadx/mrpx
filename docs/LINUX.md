# Linux 构建与运行

本文以 **Ubuntu 24.04 x86_64** 为参考，命令在 Linux shell 和仓库根目录执行。Windows 构建保留，参见 [Windows 指南](DEVELOPMENT.md)。其他发行版、CPU 架构和 macOS 不在当前验证范围内。

原生桌面使用 SDL2 显示 MRP、GTK 4.10+ 提供 21 键面板、Console 和文件选择，网络使用 POSIX sockets。Web 构建不依赖 GTK4/SDL2 的系统开发包，而使用 Emscripten 的 SDL2 port。两条链路共用根 [Makefile 用户入口](../Makefile) 和 [PowerShell 实现层](../scripts/build.ps1)，没有单独的 Linux 准备脚本。

## 1. 基础工具

原生构建所需的系统开发包：

```sh
sudo apt-get update
sudo apt-get install -y build-essential pkg-config libsdl2-dev libgtk-4-dev \
  cmake ninja-build git curl ca-certificates python3
# 可选：make debug / make de
sudo apt-get install -y libcapstone-dev
# 可选：无物理显示的 GTK 控件测试
sudo apt-get install -y xvfb dbus-x11
```

还需 **PowerShell 7.3+**。可按 [Microsoft 的 Ubuntu 安装说明](https://learn.microsoft.com/powershell/scripting/install/install-ubuntu) 使用包管理器安装；以下是固定的官方 Linux x64 便携版：

```sh
mkdir -p deps/downloads "$HOME/.local/share/mrpx/pwsh-7.7.0"
curl -fL --retry 3 \
  https://github.com/PowerShell/PowerShell/releases/download/v7.7.0/powershell-7.7.0-linux-x64.tar.gz \
  -o deps/downloads/powershell-7.7.0-linux-x64.tar.gz
echo 'c89d9c682d2c403dfe726f3df948e359675235a5bd6fab535776de69729ca0cb  deps/downloads/powershell-7.7.0-linux-x64.tar.gz' | sha256sum -c -
tar -xzf deps/downloads/powershell-7.7.0-linux-x64.tar.gz \
  -C "$HOME/.local/share/mrpx/pwsh-7.7.0"
chmod +x "$HOME/.local/share/mrpx/pwsh-7.7.0/pwsh"
export PATH="$HOME/.local/share/mrpx/pwsh-7.7.0:$PATH"
pwsh --version
```

每个新 shell 都需重新设置 PATH，或有意将该设置加入自己的 shell 配置。不要在 Linux 中加载 Windows SDK 安装生成的环境脚本，也不要把 Windows 的 DLL、npm 安装目录或构建对象当成 Linux 依赖；Linux SDK 自身提供的 `.sh`/`.ps1` 环境脚本可用于对应 shell。

## 2. 原生 Unicorn 与宿主

使用官方 **Unicorn 2.1.4**，不是 WASM TCI fork，也不是发行版中可能较旧的 `libunicorn-dev`：

```sh
mkdir -p deps
git clone --depth 1 --branch 2.1.4 \
  https://github.com/unicorn-engine/unicorn.git deps/unicorn
cmake -S deps/unicorn -B deps/unicorn/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DUNICORN_ARCH=arm -DUNICORN_BUILD_TESTS=OFF
cmake --build deps/unicorn/build --parallel 4
cp config.linux.example.mk config.local.mk
make main
./bin/main
```

Unicorn 源码路径应不含空格。`config.local.mk` 使用 GNU Make 路径语法，值不包引号。默认头文件在 `deps/unicorn/include`、静态库在 `deps/unicorn/build/libunicorn-static.a`，Make 同时链接 ARM/common 静态库；SDL2/GTK4 由 `pkg-config` 查找。也可直接用 `make native-deps JOBS=4` 准备固定 SDK，再执行 `make main`。旧本机配置仍会覆盖新默认值，若原先指定合并 `libunicorn.a`，切换到此 SDK 时须将 `UNICORN_LIB` 改为 `$(UNICORN_BUILD_DIR)/libunicorn-static.a`，不要直接覆盖整份本机配置。系统 Capstone 仅影响可选的 `make debug`、`make de`；已有合并库可通过 `UNICORN_LIB` 覆盖，不强制替换既有 SDK。

Linux 产物为 `bin/main`、`bin/main-debug` 和 `tools/de`，没有 `.exe` 后缀。对象仍在 `build/release` / `build/debug`；运行资源由 manifest 复制到程序所在的 `bin`，不写入 [只读种子](../assets/runtime/manifest.json)。`VMRP_RUNTIME_DIR` 可覆盖可写运行目录。程序默认启动 `dsm_gm.mrp` 应用列表，将自己的 MRP 放入 `bin/mythroad`，再从应用列表选择，或用 Open MRP、Ctrl+O 或拖放打开。

Linux 原生目录句柄保存按文件名字节顺序排序的快照，避免 ext4 创建顺序改变启动器应用列表和既有截图基线；打开后新增/删除的条目需重新打开目录才能反映。Windows/Web 保留原有目录读取实现。

### 显示环境与输入

交互运行需要正常的 X11/Wayland 桌面或 WSLg。GTK4、SDL2 和 Guest 派发都在宿主主线程；关闭面板仅隐藏，MRP 窗口按 **F1** 重新打开。Console 支持 GBK 中文日志、清空/复制、普通多行/数字/密码输入及提交/取消；GTK 输入框支持 **Ctrl+Enter** 提交。

无法初始化 GTK 显示时程序会报错退出，不会伪装成可交互运行。截图回归模式不创建 GTK 面板，可使用 SDL dummy 驱动。GTK 测试可在 Xvfb 中创建真实控件：

```sh
make test
make test-build
dbus-run-session -- xvfb-run -a env GSK_RENDERER=cairo make test-controls
make test-mrp
make debug de  # 安装 Capstone 后执行
```

`make test-mrp` 会运行八项 runner 单测和六个真实 MRP 截图/输入场景，沿用 [既有像素基线](../tools/mrp_regression.py)，不接受仅因平台不同而重写哈希。测试故意触发无效输入、队列拒绝等错误日志，应同时检查完成标记和退出码。网络测试包含真实本地回环收发、异步关闭和 Linux 断连发送不会触发 SIGPIPE 的场景。

## 3. Linux WASM

只构建 Web 时，系统工具需要 `make`、Git、Python 3、CMake、Ninja、curl、PowerShell；不必安装原生 SDL2/GTK4/Unicorn。SDK 使用独立 Linux 安装，并固定 **Emscripten 6.0.12**：

```sh
git clone https://github.com/emscripten-core/emsdk.git deps/emsdk
./deps/emsdk/emsdk install 6.0.12
./deps/emsdk/emsdk activate 6.0.12
source ./deps/emsdk/emsdk_env.sh
export PATH="$(dirname "$EMSDK_NODE"):$PATH"
export EM_CACHE="$HOME/.cache/mrpx/emscripten-6.0.12"
emcc --version
node --version
make web-deps
make prepare-web-audio
make wasm-unicorn WASM_CMAKE=cmake JOBS=4
make wasm
make test-wasm-unicorn test-web-controls test-web-audio
```

SDK 自带 Node **24.19.0**，音频准备要求 Node **24+**；上面的 PATH 设置同时启用 SDK 的 Node/npm，`npm ci` 只在 Linux 工作区执行。Make 的 Node 测试默认优先使用已激活 SDK 的 `EMSDK_NODE`，避免 SDK 根目录的 `node/` 目录遮蔽可执行文件，仍可通过 `NODE` 覆盖。原生 CMake `/usr/bin/cmake` 是合法入口，不需要 Windows CMake。WASM 后端固定为 [TCI fork](https://github.com/elliott-wen/unicorn.wasm) 的提交 `4ce0fc93249d0f581ed22049a36baa547ef70447`，自动应用 [项目补丁](../scripts/unicorn-wasm-2.1.4.patch) 并运行 smoke。

首次需要联网下载后端、ports 和音色；MIDI 库/音色版本、SHA-256、许可证和离线运行约束与 [Windows 复现指南](DEVELOPMENT.md#44-本地-midi-资源与分发) 相同。最终站点在 `build/web`，完整部署该目录，而不是只复制 JS/WASM。

```sh
make serve PORT=8080
```

浏览器打开：

- `http://127.0.0.1:8080/build/web/main.html`
- `http://127.0.0.1:8080/tests/wasm_mrp_browser_tests.html`：七个真实 MRP 场景。
- `http://127.0.0.1:8080/tests/web_audio_browser_tests.html`：用户手势解锁及真实音频检查。

这些测试需从 localhost 加载，不是 `file:` 页面。Node/mock 测试不替代真实浏览器与 MRP 验证。

## 4. WSL 与产物边界

建议把 Linux 检出和依赖放在 Linux 文件系统的无空格路径，例如 `~/src/mrpx`，不要与 Windows 共用 `node_modules`、`config.local.mk`、Unicorn 库或 C 对象。`/mnt/c` 下源码虽可读取，但 Windows 只读目录属性及磁盘 IO 会影响解包/编译，复制后应保证 Linux 工作区属于当前用户且可写。

`deps/`、`node_modules/`、`build/` 及两个平台的宿主/工具可执行文件均忽略；源码、配置示例、补丁、锁文件与固定 Guest 输入提交。`make clean` 仍只清理已知产物，保留 `bin/mythroad` 和旧版 `build/runtime` 应用数据、本地配置和原生 `deps`；WASM 后端缓存会被清理，需要重新准备。

## 5. 实际验证范围

参考环境为 Ubuntu 24.04 x86_64（WSL），GCC 13.3.0、CMake 3.28.3、SDL2 2.30.0、GTK4 4.14.5、Capstone 4.0.2、PowerShell 7.7.0、Node 24.19.0 和 Emscripten 6.0.12。

已验证原生 main/debug/de 构建、五组 C 测试、八项 runner 单测、六个真实 MRP 场景、构建入口安全测试，以及 Xvfb 中真实 GTK 控件和非截图 MPC 启动。六个桌面像素基线与 Windows 相同，未因平台差异修改预期哈希。

Linux 独立 SDK、TCI 后端、Unicorn smoke、WASM 宿主编译、npm 安装、实际 MIDI 打包与 13 项 JavaScript 测试均已通过。WASM 构建还注入了禁止展开原生 `pkg-config` 的检查，确认不依赖系统 GTK4/SDL2 开发包。

Linux 生成的完整站点在 Windows Edge 中通过七个真实 MRP 场景，以及 AudioWorklet 播放、循环、结束静音和停止静音检查；沿用原有预期哈希，不复用 Windows 编译的 WASM。这不等同于 Linux 浏览器音频设备验收。物理 Wayland/X11、WSLg、其他 CPU 架构和发行版尚未验证。
