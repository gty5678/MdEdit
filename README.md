# MdEdit

基于 Qt 6 / C++17 / CMake / Ninja 的单文档 Markdown 编辑器，使用 vcpkg 管理依赖，提供 Windows、macOS 和 Linux 构建配置。

“视图”菜单提供“Live 编辑 / 源码”切换功能。默认在同一文档区域直接编辑：光标进入段落时显示该段 Markdown 源码，离开后恢复排版与公式。空行分隔编辑段，代码块和多行公式保持完整。Ctrl+E 可切换为全文源码编辑。快捷键中的 Ctrl 在 macOS 上对应 Cmd；应用采用单视口，不提供分屏模式。

## 使用

Windows 打包后运行 `dist/windows/bin/MdEdit.exe`。在启动页点击“打开”，或按 Ctrl+O 选择 Markdown 文件。

- Ctrl+E：切换 Live / 全文源码编辑，保留光标位置和选区方向。
- Live 模式中，Enter 自动延续无序列表、有序列表和任务列表；新任务默认未勾选，空列表项按 Enter 退出列表。
- Shift+Enter 仅插入换行；代码围栏和多行公式中 Enter 也仅插入换行。
- Live 模式中，代码块在非编辑状态将围栏行留空，点击后显示完整源码；围栏行始终保留高度，切换时排版不跳动。支持 C/C++、Python、JavaScript/TypeScript、JSON、Shell 和 LaTeX 的基础语法高亮；未标注或未知语言以等宽字体显示。

代码块右上角提供“复制代码”按钮，可将代码正文复制到剪贴板。

停止输入片刻后，修改会以 UTF-8 原子写回原文件。关闭窗口时会立即保存尚未写入的修改。也可通过命令行参数打开文件。支持 Markdown 标题、列表、表格、代码块和相对路径图片。

Live 编辑中，输入 `*`、`**`、`***`、`~~` 或 `==` 后，后续文字立即显示斜体、加粗、加粗斜体、删除线或高亮，直到配对标记闭合或当前行结束；代码与转义标记不触发样式。

正文支持 `==文字==` 浅黄色背景高亮，可与加粗、斜体和删除线组合；代码中的标记保持原样。

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

## 表格编辑

原生 Markdown 表格可直接点击单元格编辑，支持中文输入法、复制/剪切/粘贴、加粗和斜体，修改同步回 Markdown 源码并可撤销。

- Tab / Shift+Tab 切换单元格，末格按 Tab 自动追加一行。
- 右键菜单可插入、移动和删除行列，至少保留一行一列；列移动保留对齐设置。
- 鼠标移到行左侧或列顶部边缘时显示手柄，点击选择整行/列，拖动调整顺序；选中后可复制、剪切或按 Delete 删除。
- 鼠标移到表格底部或右侧边缘时显示“＋”，用于追加行或列；“编辑”菜单也可插入新表格。

CSV 使用独立编辑面板，详见下方资源预览说明。

## 外部资源预览

在独立段落中使用图片引用语法嵌入本地资源，相对路径以当前 Markdown 文件所在目录为基准；含空格路径可用尖括号包裹：

```markdown
![图片说明|400](assets/example.png)

![PDF 文档](<assets/example document.pdf>)

![数据表格](assets/data.csv)

![文本摘要](assets/notes.txt)
```

资源块提供“编辑源码”按钮来修改引用；点击预览或将光标移入不会自动展开引用源码。图片、PDF、CSV 和文本共用异步资源加载与缓存。预览仅加载本地文件，单文件上限 32 MiB；加载失败时显示错误。

### 图片

支持 PNG、SVG、JPG、JPEG、BMP、GIF、WebP、TIF、TIFF，扩展名不区分大小写。`![说明|400](图片路径)` 中的数字指定显示宽度（逻辑像素），高度按比例计算，独立图片和行内图片均支持。未指定宽度时自动调整尺寸。

### CSV

CSV 在正文中显示前 8 行、8 列的摘要，双击打开完整文件编辑面板。面板支持单元格修改、行列插入、删除和移动；应用时直接原子保存原 CSV，保留 BOM 和换行类型，并正确转义逗号、双引号与多行字段。

编辑上限为 8 MiB、10000 行、256 列。保存前会检查文件是否被外部修改，冲突时提示并保留编辑面板。CSV 修改不改变 Markdown 正文，也不进入正文撤销历史。

### 文本与附件

TXT、LOG、JSON/JSONL、XML、YAML、TOML、INI/CFG/CONF、Markdown、TSV 及常见代码文件支持只读摘要。内容按原文显示，不执行代码或解释 HTML/Markdown；最多预览前 12 行、8000 个 UTF-16 单元，截断时显示提示，可通过“打开原文件”查看全文。

支持 UTF-8（含 BOM）和带 BOM 的 UTF-16/UTF-32；解码失败时显示错误。不支持预览的文件显示单行附件条，点击可用系统默认程序打开。

### PDF

PDF 使用 Qt PDF Widgets 查看器，按宽度适配并连续显示多页。可在块内滚动、拖动滚动条、使用“上一页 / 下一页”或输入页码跳转，也可通过“打开原文件”调用系统阅读器。阅读和翻页不会修改 Markdown 或重建正文布局。

只有到达整份 PDF 顶部后继续向上滚动，或到底部后继续向下滚动，才滚动外层正文。加密或损坏文件显示错误提示。

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

这两个预设使用 `$HOME/Qt/6.12.0/macos` 和本次已准备的 `build-local/qt612-deps`（使用同一官方 Qt 重新编译的 MicroTeX），tinyxml2 来自 `build-vcpkg/vcpkg_installed/arm64-osx`。这些依赖目录需要保留；换机器或删除依赖后需要重新准备。它们与原有 vcpkg 构建预设使用不同的构建目录。

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

## Obsidian 行为对照

完整功能域、现有差异和待补项见 [Obsidian 行为对照](docs/obsidian-behavior-comparison.md)。新增“文件”和“编辑”菜单，支持打开、保存、加粗、斜体、行内代码、链接、段落复制/剪切、缩进、删除当前行及查找替换。快捷键中的 Ctrl 在 macOS 上对应 Cmd。当前查找为字面文本匹配，可区分大小写并循环查找；替换支持单次与全部，操作可撤销。菜单中的快捷键以实际平台显示为准。

基础字号可在“文件 → 设置”中调整（macOS 也可能显示在应用的设置菜单），快捷键 Ctrl/Cmd+,。支持 9–30 pt，默认 14 pt；即时更新正文、源码、界面和行号，标题按层级比例缩放。设置自动保存，重启后保留，并提供“恢复默认值”。

编辑区已封装为可嵌入的 `MarkdownEditorWidget`（`QWidget`），可链接 `MdEdit::Editor` 复用。接口、资源初始化与示例见 [嵌入指南](docs/embedding.md)。


## 测试与实现边界

配置并构建后，使用对应测试预设执行回归，例如 `ctest --preset macos-qt-debug`；也可针对已有构建目录运行：

```bash
ctest --test-dir build-macos-qt612 --output-on-failure
```

当前 CTest 包含 architecture、table-edit、live-pdf、live-csv、typing-style、highlight、line-spacing、formula-vector、embedded-editor 和 markdown-math-smoke 共 10 项。综合示例见 [综合测试.md](综合测试.md)。

资源编辑器仍在持续完善，尚未实现远程资源加载、外部文件自动监视、DXF/GLB 预览和大表格虚拟化。独立预览基础组件与应用 Live 编辑器的能力范围不同；模块边界与实现限制见 [基础版本说明](docs/resource-editor-foundation.md)。
