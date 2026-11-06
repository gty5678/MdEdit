# MdEdit

基于 Qt 6 / C++17 / CMake / Ninja 的单文档 Markdown 编辑器，使用 vcpkg 管理依赖，支持 Windows、macOS 和 Linux 构建。

界面顶部提供“打开 / 保存 / Live 编辑”。默认在同一文档区域直接编辑：光标进入段落时显示该段 Markdown 源码，离开后恢复排版与公式。空行分隔编辑段，代码块和多行公式保持完整。Ctrl+E 可切换为全文源码编辑。

## 使用

Windows 打包后运行 `dist/windows/bin/MdEdit.exe`。直接输入文字，或按 Ctrl+O 打开 Markdown 文件。

- Ctrl+O：打开文件
- Ctrl+S：保存
- Ctrl+Shift+S：另存为
- Ctrl+E：切换 Live / 全文源码编辑

采用 UTF-8、原子文件保存，打开其他文件或退出前会询问是否保存修改。也可通过命令行参数打开文件。支持 Markdown 标题、列表、表格、代码块和相对路径图片。

## 数学公式

非当前段落显示公式排版；进入公式所在段落可直接修改 LaTeX 源码。

```markdown
行内公式：$E = mc^2$

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

$$
\begin{pmatrix} a & b \\ c & d \end{pmatrix}
$$
```

使用内置 MicroTeX 与随程序部署的字体离线排版，不需要浏览器、网络或系统 LaTeX。代码块及行内代码不识别为公式。无法解析的公式保留源码，预览按钮的提示显示错误。公式兼容范围以 MicroTeX 为准，不支持完整 LaTeX 文档或任意宏包。

## 构建

需要 Git、CMake 3.24+、Ninja 和 C++17 编译器。所有第三方依赖由根目录 `vcpkg.json` 管理，registry baseline 固定版本；Qt 最低支持 6.5，实际版本由 baseline 决定，不再要求本机安装 Qt 6.12.0。

先按 [vcpkg 官方说明](https://learn.microsoft.com/vcpkg/get_started/get-started) 安装并 bootstrap vcpkg，将环境变量 `VCPKG_ROOT` 指向该目录。首次配置会下载并编译 Qt 等依赖，耗时和磁盘占用较大；后续使用 vcpkg 缓存。

Windows：安装 Visual Studio 的“使用 C++ 的桌面开发”，打开 **x64 Native Tools Command Prompt**：

```bat
set "VCPKG_ROOT=C:\dev\vcpkg"
build.cmd
```

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

通用命令（三个平台一致）：

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
cmake --install build-vcpkg --prefix dist/local
```

`windows-release`、`macos-release`、`linux-release` 是同一配置的别名。构建产物位于 `build-vcpkg`，避免复用旧构建缓存。可在配置命令后传入 `-DVCPKG_TARGET_TRIPLET=arm64-osx` 等参数；更换 triplet、编译器后请使用新构建目录。使用 vcpkg 预设时不要混用官方 Qt kit 和 vcpkg Qt；清除旧的 `Qt6_DIR`、`CMAKE_PREFIX_PATH` 设置。

Windows/macOS 测试使用原生平台插件，Linux 使用 offscreen 插件（测试无需显示服务器）；覆盖编辑操作、行内公式、分式、积分、矩阵与代码区域保护，生成 `build-vcpkg/math-smoke.png`。macOS/Linux 仍需在对应系统执行构建、测试和 GUI 检查。Linux 安装目录包含资源与许可证，但不保证可跨发行版直接运行；Qt 的运行依赖部署支持取决于该 Qt 版本和系统。

## VS Code（本机 macOS / Qt 6.12.0）

已在 `.vscode/settings.json` 为 CodeLLDB 配置 Command Line Tools 自带的 `debugserver` 路径。已配置 CMake Tools、C/C++ 和 CodeLLDB 扩展推荐；本机已安装这些扩展。使用 VS Code 打开项目根目录后：

- 打开“运行和调试”，选择 `MdEdit Debug`，按 F5 自动配置、编译并启动调试，可在 `src/main.cpp` 设置断点。
- 选择 `MdEdit Release`，按 F5 构建并启动 Release；优化后的代码可能无法逐行调试。按 Ctrl+F5 可直接运行。
- 按 Cmd+Shift+B 默认构建 Debug；通过“任务：运行任务”选择 `CMake: Build Release` 可只构建 Release。
- CMake Tools 的配置预设选择 `macos-qt-debug` 或 `macos-qt-release`，分别使用独立构建目录 `build/macos-qt-debug`、`build/macos-qt-release`。

这两个预设使用 `$HOME/Qt/6.12.0/macos` 和本次已准备的 `build-local/qt612-deps`（使用同一官方 Qt 重新编译的 MicroTeX），tinyxml2 来自 `build-vcpkg/vcpkg_installed/arm64-osx`。这些依赖目录需要保留；换机器或删除依赖后需要重新准备。它们与原有 vcpkg 构建预设使用不同的构建目录。

也可在终端执行：

```bash
cmake --preset macos-qt-debug
cmake --build --preset macos-qt-debug --parallel
ctest --preset macos-qt-debug
# Release 将上述预设名替换为 macos-qt-release
```

## 第三方依赖

- Qt：vcpkg `qtbase`（Widgets、PNG/JPEG、各平台后端）和 `qtsvg`（SVG 图片）。
- tinyxml2：vcpkg 官方 port。
- [MicroTeX](https://github.com/NanoMichael/MicroTeX)：项目的 `ports/microtex` overlay port，固定上游提交及 SHA512，构建 Qt 6 静态后端；通过补丁保留 Qt 6 字体 API 和资源路径修复，不编译示例。应用通过 `find_package(MicroTeX)` 使用安装后的库。

字体资源从 vcpkg 安装目录部署到可执行文件旁的 `res`（macOS 为 `Contents/Resources/res`）；依赖许可证复制到 `licenses`。升级依赖请运行 `vcpkg x-update-baseline` 并重新验证。升级 MicroTeX 时需同时更新提交、SHA512 和兼容补丁。

项目在 `vcpkg-configuration.json` 中明确使用官方 Git registry（`https://github.com/Microsoft/vcpkg`），并固定 baseline，避免本机针对小写组织名 URL 的镜像重定向导致 `not our ref`。需要能访问 GitHub；若仍失败，查看 `build-vcpkg/vcpkg-manifest-install.log`，不要移除版本锁定。Visual Studio 中修改配置后请执行“删除缓存并重新配置”；命令行可执行 `cmake --fresh --preset windows-release`。
