# MdEdit 开发指南

[← 返回 README](../README.md)

以下命令均在项目根目录执行。本机 Qt 预设包含机器相关的依赖路径，换机器后需按实际安装位置调整。

## 源码结构

- `src/main.cpp`：应用初始化、公式引擎生命周期与启动入口。
- `src/platform.h`：平台字体、快捷键标签和公式资源路径。
- `src/markdown/scanner.*`：代码/公式边界扫描、代码正文提取与词法着色。
- `src/markdown/renderer.*`：Markdown 排版、共享文本样式与公式图像处理。
- `src/editor/highlighter.*`：全文源码语法高亮。
- `src/editor/numbered_editor.h`：行号组件与通用编辑器模板。
- `src/editor/live_editor.*`：Live 编辑、源文档位置映射、输入事件与排版缓存。
- `src/editor/markdown_editor_widget.*`：可嵌入组件、编辑命令与模式切换。
- `src/editor/markdown/block_parser.*`：文档块识别与 UTF-16 源码范围。
- `src/editor/resources/resource_manager.*`：本地资源解析、异步加载与缓存。
- `src/editor/render/renderer_registry.*`：资源预览绘制分派。
- `src/editor/tables/`：表格数据模型与 CSV 编辑面板。
- `src/editor/view/markdown_preview_widget.*`：独立预览基础组件。
- `src/window.*`：主窗口、菜单、文件读写和设置。
- `tests/app_tests.*`：现有 `--smoke-test`、`--edit-menu-test`、`--table-gutter-test` 测试入口；保留原命令行调用方式。

## 构建

需要 Git、CMake 3.24+、Ninja 和 C++17 编译器。Qt 最低要求 6.12.0，需包含 Widgets、SVG、PDF 和 PDF Widgets 模块。当前 vcpkg baseline 提供 Qt 6.11.3，使用该 baseline 的 Windows、macOS 和 Linux 预设暂时无法满足版本要求。macOS 可使用下文的本机 Qt 6.12.0 预设。

PDF 预览依赖 `Qt6::Pdf` 和 `Qt6::PdfWidgets`，本机 Qt kit 需通过 Qt Maintenance Tool 安装匹配的 Qt PDF 模块。vcpkg 清单使用 `qtwebengine` 的 `pdf` feature，关闭默认 features，不启用 WebEngine 浏览器。打包时需一并部署 Qt PDF 与 PDF Widgets 运行库。

先按 [vcpkg 官方说明](https://learn.microsoft.com/vcpkg/get_started/get-started) 安装并 bootstrap vcpkg，将环境变量 `VCPKG_ROOT` 指向该目录。首次配置会下载并编译 Qt 等依赖，耗时和磁盘占用较大；后续使用 vcpkg 缓存。

### Windows（MinGW 打包）

安装 VS Code 和 [MSYS2](https://www.msys2.org/)。在 **MSYS2 MINGW64** 终端安装工具链：

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

将 MSYS2 的 `mingw64\bin`（默认 `C:\msys64\mingw64\bin`）加入 Windows 用户 `Path`，并将 `VCPKG_ROOT` 设为已运行 `bootstrap-vcpkg.bat` 的 vcpkg 目录。vcpkg 的目标和宿主 triplet 均为 `x64-mingw-dynamic`。本机 VS Code 调试步骤见下文的 Windows Qt 6.12.0 配置。

- 运行任务 `MdEdit: Package Windows`，或在 VS Code 集成终端执行 `.\build.cmd`。脚本会构建 Release、运行冒烟测试并安装到 `dist/windows`。

Windows 使用 `build/windows-debug` 和 `build/windows-release`，共用 `build/windows-deps/vcpkg_installed`。已有的旧版 Windows 构建缓存可以留在原目录，新预设会在独立目录重新配置。需要清除新预设缓存时，在 CMake Tools 中运行“Delete Cache and Reconfigure”，或执行 `cmake --fresh --preset windows-release`。

### macOS 和 Linux

macOS：安装 Xcode Command Line Tools、CMake 和 Ninja：

```bash
brew install cmake ninja autoconf autoconf-archive automake libtool
export VCPKG_ROOT="$HOME/vcpkg"
bash build-macos.sh
```

如果项目目录内已有 bootstrap 完成的 `.vcpkg`，脚本会自动使用它，无需设置 `VCPKG_ROOT`。

生成 `dist/macos/MdEdit.app` 和 `dist/macos/MdEdit.dmg`，并从临时目录运行打包后的冒烟测试。默认使用当前机器架构；切换架构时应使用新的构建目录，并匹配 vcpkg triplet。应用未签名、公证。

Linux：安装 C++ 工具链、CMake、Ninja、pkg-config 和 Qt XCB 后端需要的系统开发包。Debian/Ubuntu 可从以下包开始，具体以 vcpkg 配置诊断为准：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config autoconf automake libtool \
  libgl1-mesa-dev libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev \
  libxrender-dev libxcb1-dev libxcb-cursor-dev libxcb-image0-dev libxcb-keysyms1-dev \
  libxcb-randr0-dev libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
  libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-x11-dev
export VCPKG_ROOT="$HOME/vcpkg"
bash build-linux.sh
```

各平台的 vcpkg Release 预设为 `windows-release`、`macos-release` 和 `linux-release`。macOS 和 Linux 的 Release 构建目录是 `build-vcpkg`。更换 triplet 或编译器时请使用新的构建目录。使用 vcpkg 预设时，应清除此前为官方 Qt kit 设置的 `Qt6_DIR` 或 `CMAKE_PREFIX_PATH`。

Linux 可在 VS Code 里用 `MdEdit Debug (Linux)` 调试，系统需安装 GDB，并设置好 `VCPKG_ROOT`。Debug 使用 `linux-debug` 预设，产物位于 `build/linux-debug`。

architecture、table-edit、live-pdf 和 live-csv 测试统一使用 offscreen 插件；其他 CTest 用例在 Windows/macOS 使用原生平台插件，Linux 使用 offscreen 插件。测试覆盖编辑操作、资源加载、表格、PDF、行内样式、行距、嵌入组件及公式排版；冒烟测试生成 `math-smoke.png`。Linux 安装目录包含资源与许可证，运行依赖的部署支持取决于 Qt 版本和系统。

## VS Code（本机 Windows x64 / Qt 6.12.0）

用 VS Code 打开项目根目录并安装 Microsoft C/C++ 扩展（`ms-vscode.cpptools`）。在“运行和调试”中选择 **MdEdit Debug (Windows)**，打开 `src/main.cpp` 设置断点，然后按 F5。构建任务自动加载 Visual Studio 的 x64 MSVC 环境，配置并编译 Debug 版本，并将 Qt DLL 和平台插件部署到 `build/windows-qt-debug`。Ctrl+F5 可直接运行。

此预设使用本机 `C:/Qt/6.12.0/msvc2022_64` 和 `build/windows-debug-deps/x64-windows` 下用相同工具链编译的 Debug 版 tinyxml2 和 MicroTeX。后者位于被 Git 忽略的 `build` 目录。换机器或清空该目录后，需要重新构建这两个 Debug 依赖并安装到该前缀。MicroTeX 使用 `ports/microtex` 中的源码版本、补丁及 CMake 配置。

## VS Code（本机 macOS / Qt 6.12.0）

已在 `.vscode/settings.json` 为 CodeLLDB 配置 Command Line Tools 自带的 `debugserver` 路径。已配置 CMake Tools、C/C++ 和 CodeLLDB 扩展推荐；需安装这些扩展。使用 VS Code 打开项目根目录后：

- 打开“运行和调试”，选择 `MdEdit Debug (macOS)`，按 F5 自动配置、编译并启动调试，可在 `src/main.cpp` 设置断点。
- 选择 `MdEdit Release (macOS)`，按 F5 构建并启动 Release；优化后的代码可能无法逐行调试。按 Ctrl+F5 可直接运行。
- 按 Cmd+Shift+B 默认构建 Debug；通过“任务：运行任务”选择 `CMake: Build Release` 可只构建 Release。
- CMake Tools 的配置预设选择 `macos-qt-debug` 或 `macos-qt-release`，分别使用独立构建目录 `build/macos-qt-debug`、`build/macos-qt-release`。

这两个预设使用 `$HOME/Qt/6.12.0/macos` 和本机准备的 `build-local/qt612-deps`（使用同一官方 Qt 重新编译的 MicroTeX），tinyxml2 来自 `build-vcpkg/vcpkg_installed/arm64-osx`。这些依赖目录需要保留；换机器或删除依赖后需要重新准备。它们与原有 vcpkg 构建预设使用不同的构建目录。

也可在终端执行：

```bash
cmake --preset macos-qt-debug
cmake --build --preset macos-qt-debug --parallel
ctest --preset macos-qt-debug
# Release 将上述预设名替换为 macos-qt-release
```

## 第三方依赖

- Qt：vcpkg `qtbase`（Widgets、PNG/JPEG、各平台后端）、`qtsvg`（SVG 图片）和 `qtimageformats`（WebP、TIFF）。图片仅支持 PNG、SVG、JPG、JPEG、BMP、GIF、WebP、TIF、TIFF；其他文件显示单行附件，可用系统默认程序打开。
- tinyxml2：vcpkg 官方 port。
- [MicroTeX](https://github.com/NanoMichael/MicroTeX)：项目的 `ports/microtex` overlay port，固定上游提交及 SHA512，构建 Qt 6 静态后端；通过补丁保留 Qt 6 字体 API 和资源路径修复，不编译示例。应用通过 `find_package(MicroTeX)` 使用安装后的库。

字体资源从 vcpkg 安装目录部署到可执行文件旁的 `res`（macOS 为 `Contents/Resources/res`）；依赖许可证复制到 `licenses`。升级依赖请运行 `vcpkg x-update-baseline` 并重新验证。升级 MicroTeX 时需同时更新提交、SHA512 和兼容补丁。

项目在 `vcpkg-configuration.json` 中明确使用官方 Git registry（`https://github.com/Microsoft/vcpkg`），并固定 baseline，避免本机针对小写组织名 URL 的镜像重定向导致 `not our ref`。需要能访问 GitHub；若仍失败，查看 `build-vcpkg/vcpkg-manifest-install.log`，不要移除版本锁定。

## 测试与实现边界

配置并构建后，使用对应测试预设执行回归，例如 `ctest --preset macos-qt-debug`；也可针对已有构建目录运行：

```bash
ctest --test-dir build-macos-qt612 --output-on-failure
```

当前 CTest 包含 architecture、table-edit、live-pdf、live-csv、typing-style、highlight、line-spacing、formula-vector、embedded-editor 和 markdown-math-smoke 共 10 项。综合示例见 [综合测试.md](../综合测试.md)。

资源编辑器仍在持续完善，尚未实现远程资源加载、外部文件自动监视、DXF/GLB 预览和大表格虚拟化。独立预览基础组件与应用 Live 编辑器的能力范围不同；模块边界与实现限制见 [基础版本说明](resource-editor-foundation.md)。
