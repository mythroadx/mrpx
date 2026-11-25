# Tag 发布与软件包

[GitHub Actions 工作流](../.github/workflows/release.yml) 仅由推送 tag 触发。tag 必须是完整的稳定版本 `vX.Y.Z`，例如 `v1.2.3`、`v0.0.1`；三个数字不允许前导零。`v1.2`、`v1.2.3-rc.1`、`v1.2.3+build`、`V1.2.3` 不发布。

## 1. 产物

每次发布生成三个 ZIP 和一个 `SHA256SUMS.txt`：

项目名称统一为 `mrpx`，包内站点加载 `mrpx.js` / `mrpx.wasm`。`SOURCES.txt` 保留原 VMRP 来源；CI 生成当前发布仓库及 tag 的源码链接，本地打包则明确标注尚未提供发布源码 URL，不把原仓库链接冒充 mrpx 源码地址。

| 文件 | 内容与运行要求 |
| --- | --- |
| `mrpx-vX.Y.Z-windows-amd64.zip` | Windows x64 原生程序、实际导入的 SDL2/工具链 DLL、固定 Guest 种子、许可证；运行 `bin/main.exe` |
| `mrpx-vX.Y.Z-linux-amd64.zip` | Ubuntu 24.04 x86_64 原生程序、固定种子、启动脚本、许可证；使用系统 GTK4/SDL2 运行库 |
| `mrpx-vX.Y.Z-wasm.zip` | 完整静态站点 `web/`，以及 Windows/Linux x64 两个内嵌该站点的 Go 服务器 |

原生包将可执行文件、`cfunction.ext` 和 `mythroad/` 放在同一个 `bin/` 目录，不需要用户安装 Unicorn SDK。默认启动 `dsm_gm.mrp` 应用列表；将 MRP 放入 `bin/mythroad`，再从应用列表选择，或用 Open MRP、Ctrl+O 或拖放打开。Windows、Linux 和 WASM 发行包均不包含 MPC 或阅读器；MPC 仅保留为测试夹具。资源只从 [manifest](../assets/runtime/manifest.json) 所列的源码种子复制，不打包本机应用数据。Windows DLL 从构建 SDK/编译器安装目录递归收集，未知依赖直接报错，不从源码仓库或系统目录拼凑。

Linux ZIP 不内置 GTK4/SDL2 或 glibc。Ubuntu 24.04 桌面上运行：

```sh
sudo apt-get install libsdl2-2.0-0 libgtk-4-1
./run.sh
```

ZIP 明确保留 Linux 程序和启动脚本的可执行权限，包括在 Windows 上交叉编译出来的 Go Linux 服务器。其他 Linux 发行版、CPU 架构、无桌面显示的原生运行不在发布支持范围内。

## 2. Go 内嵌服务器

[服务器源码](../webserver/main.go) 仅使用 Go 标准库，`go:embed` 内嵌完整站点；不依赖运行目录、不读外部站点文件、不需要 Node/npm/Emscripten。CI 固定 Go 1.26.6，使用 `CGO_ENABLED=0` 交叉生成 Windows/Linux amd64 二进制。

解压 WASM ZIP 后任选对应程序：

```powershell
.\mrpx-web-windows-amd64.exe
```

```sh
./mrpx-web-linux-amd64
```

打开 `http://127.0.0.1:8080/`。端口可以用 `-listen 127.0.0.1:9000` 修改；绑定公网地址属于显式部署选择。服务器不自动打开浏览器，不提供上传或宿主命令接口，禁用目录列表，支持 HEAD/Range/WASM MIME。

两个程序可单独移动，删除 ZIP 的 `web/` 后仍能运行。`web/` 是额外提供的独立静态部署包，不是服务器依赖。公网 MIDI AudioWorklet 需要 HTTPS，应使用反向代理或 HTTPS 静态托管；默认 localhost 满足安全上下文要求。

源码目录的 [占位文件](../webserver/site/.gitkeep) 只为允许直接运行 Go 单测，不是可用站点。不要将普通 `go build ./webserver` 产物当作发行版；使用统一打包入口生成完整内嵌版本。站点/交叉编译临时目录均位于忽略的 `build/releases`，不写入工程源码。

## 3. 构建与验证

统一用户入口是根 [Makefile](../Makefile)，`make help` 列出全部项目命令。[scripts/build.ps1](../scripts/build.ps1) 是依赖准备与打包的实现层，不重复维护编译依赖图。

原生 CI 从校验 SHA-256 的官方源码归档构建 Unicorn 2.1.4 ARM 静态库。Windows 下载校验后的 SDL2 2.32.10 MinGW SDK；Linux 使用 Ubuntu 24.04 的 SDL2/GTK4 开发包。`native-deps` 在两平台的静态构建都输出 `libunicorn-static.a`，需同时链接 ARM/common 静态库，根 Makefile 已统一处理。CI 显式设置该库路径；使用其他 SDK 提供的合并 `libunicorn.a` 时仍可通过 `UNICORN_LIB` 配置。

```powershell
make native-deps JOBS=4
```

该命令显式准备依赖，不覆盖非空的既有第三方源码目录。Unicorn 源码路径必须无空格；仓库路径有空格时，使用 `"UNICORN_ROOT=C:/sdk/unicorn-2.1.4"`。Windows 的 `NATIVE_CMAKE` 必须指向原生 CMake，不能使用 MSYS `usr\bin\cmake.exe`。

Windows SDK 解压固定使用系统自带的 `System32\tar.exe`，避免 PATH 中的 MSYS tar 将绝对路径的盘符误认为远程主机。
Windows CI 使用 `setup-msys2` 返回的安装位置配置 PATH，不混用 runner 预装的另一套 MSYS2 工具。

构建时配置 `UNICORN_ROOT`、`UNICORN_BUILD_DIR`、`UNICORN_LIB` 和 Windows `SDL2_ROOT` 指向上述 SDK；具体环境见 [Windows 指南](DEVELOPMENT.md) / [Linux 指南](LINUX.md)。原生测试包含五组 C 测试、真实控件、八项 runner 单测、六个 MRP 场景，以及 `make test-startup` 的默认应用列表启动、缺失启动器报错和程序同目录加载测试。

WASM CI 在 Linux 独立安装 Emscripten 6.0.12，使用固定 TCI fork/补丁、锁定的 npm 包和音色。执行 Unicorn smoke、13 项 JS 测试、构建入口安全测试、Go/ZIP 单测、七个真实浏览器 MRP 场景及真实 AudioWorklet 播放/静音测试。

编译成功后打包，不会自动用已有的本机资源目录代替种子：

```powershell
make package VERSION=v1.2.3 PLATFORM=windows
make package VERSION=v1.2.3 PLATFORM=linux
make package VERSION=v1.2.3 PLATFORM=wasm
```

原生包需在目标 OS 执行，非默认 SDK 可传入 `UNICORN_ROOT` / `SDL2_ROOT`。WASM 打包还需要 Go，且需先完成 `prepare-web-audio`、`wasm-unicorn`、`wasm` 构建。`PLATFORM` 默认当前原生平台，`VERSION` 必须显式提供，打包入口会校验版本格式。ZIP 位于 `build/releases/`，仅包含目标版本的顶层目录、程序、资源、说明与许可证。

本地验证工具：

```powershell
make test-package test-build test-go
```

浏览器 runner 使用 Node 24+，默认 Windows Edge / Linux `google-chrome`，可用 `CHROME_PATH` 指定浏览器；需要仓库根目录 HTTP 服务提供测试页与已构建的 Web 站点，不能只使用发行包内的 Go 生产服务器：

```powershell
make serve PORT=18766
# 在另一终端：
make test-browser BROWSER_URL=http://127.0.0.1:18766
```

## 4. 推送与发布

先提交所有工程改动，特别是工作流、新源码目录、固定资源及锁文件；未提交文件不会进入 tag/CI 检出。确认 tag 指向准备发行的提交，再执行：

```sh
git tag -a v1.2.3 -m "Release v1.2.3"
git push origin v1.2.3
```

Windows、Linux、WASM 三组任务全部成功后，发布任务检查三个 ZIP 的完整性并生成 SHA-256 清单，先上传到 draft Release，再一次性公开。构建失败不会生成公开 Release；上传失败可能留下 draft，需检查并处理后再重跑。已存在同名 Release 会明确失败，不覆盖已公开的软件包。

工作流默认只有只读仓库权限；仅最终发布任务获得 `contents: write`，使用 Actions 自带的短期 `GITHUB_TOKEN`，不需要额外个人令牌。第三方 Actions 使用固定提交 SHA。该流程不自动提交代码、创建 tag 或推送仓库。
