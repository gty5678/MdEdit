<div align="center">
  <h1>MdEdit</h1>
  <p><strong>专注书写，资源随行</strong></p>
  <p>一个本地 Markdown 桌面编辑器，支持 Live 编辑、离线公式排版与文档资源预览。</p>
  <br />

![Qt 6.12.0][badge-qt]
![C++17][badge-cpp]
![CMake 3.24+][badge-cmake]
![Platform][badge-platform]

<br />

[📖 使用说明](#usage)
[🛠 开发指南](docs/development.md)
[🧩 嵌入指南](docs/embedding.md)
[💬 问题反馈][link-issues]

</div>

<p align="center">
  <a href="#download">获取与运行</a> •
  <a href="#features">特性</a> •
  <a href="#usage">使用说明</a> •
  <a href="#resources">资源预览</a> •
  <a href="#privacy">隐私与数据</a> •
  <a href="#development">开发与技术</a> •
  <a href="#references">参考与依赖</a>
</p>

---

<a id="download"></a>

## 获取与运行

项目提供 **Windows、macOS 和 Linux** 构建配置，构建步骤见 [开发指南](docs/development.md)。

| **平台** | **构建后运行** |
| --- | --- |
| **Windows** | 打包后运行 `dist/windows/bin/MdEdit.exe` |
| **macOS** | 打包后打开 `dist/macos/MdEdit.app`，另生成 `MdEdit.dmg` |
| **Linux** | 打包安装到 `dist/linux`，运行 `dist/linux/bin/MdEdit` |

> 当前工程要求 Qt 6.12.0，仓库固定的 vcpkg baseline 提供 Qt 6.11.3，现有 vcpkg 预设尚不能直接满足版本要求。本机 Qt 6.12.0 配置、依赖准备与打包限制请先阅读开发指南。

---

<a id="features"></a>

## 特性

### 已实现

| **功能** | **说明** | **状态** |
| --- | --- | --- |
| **Live 编辑** | 在同一文档区域编辑，当前段落显示源码，离开后恢复排版 | ✅ |
| **源码模式** | 一键切换全文源码，保留光标与选区方向 | ✅ |
| **Markdown 排版** | 标题、列表、任务列表、引用、代码块与表格 | ✅ |
| **行内样式** | 加粗、斜体、删除线与 `==高亮==`，输入时即时显示 | ✅ |
| **数学公式** | MicroTeX 离线排版，支持行内与多行公式 | ✅ |
| **代码块** | 基础语法高亮与一键复制代码正文 | ✅ |
| **表格编辑** | 单元格直接编辑，行列操作、手柄拖拽与边缘追加 | ✅ |
| **图片预览** | 常见图片格式与自定义显示宽度 | ✅ |
| **PDF 阅读** | 连续多页预览、内部滚动与页码跳转 | ✅ |
| **CSV 编辑** | 正文摘要预览、独立编辑面板与冲突检查 | ✅ |
| **文本与附件** | 本地文本摘要预览，附件使用系统默认程序打开 | ✅ |
| **文件保存** | UTF-8 自动保存与原子写入 | ✅ |
| **编辑工具** | 查找替换、缩进、段落操作与撤销 | ✅ |
| **字号设置** | 9–30 pt，默认 14 pt，设置重启后保留 | ✅ |
| **组件嵌入** | 通过 `MdEdit::Editor` 在其他 Qt 应用中复用 | ✅ |

### 待完善

| **方向** | **当前边界** | **状态** |
| --- | --- | --- |
| **资源更新** | 尚未自动监视外部文件变化 | 待实现 |
| **更多资源类型** | 尚未实现 DXF、GLB 预览 | 待实现 |
| **大文档与大表格** | 尚未实现完整虚拟布局与大表格虚拟化 | 待完善 |
| **Markdown 兼容** | 复杂嵌套、跨块链接定义和源码映射仍需完善 | 持续完善 |

实现范围与后续工作见 [资源编辑器基础说明](docs/resource-editor-foundation.md)；功能差异见 [Obsidian 行为对照](docs/obsidian-behavior-comparison.md)。

---

<a id="usage"></a>

## 使用说明

应用采用单文档、单视口编辑。“视图”菜单可切换 **Live 编辑 / 源码**；空行分隔编辑段，代码块和多行公式保持完整。

### 编辑与快捷键

在启动页点击“打开”，或按 Ctrl+O 选择 Markdown 文件。

- Ctrl+E：切换 Live / 全文源码编辑，保留光标位置和选区方向。
- Live 模式中，Enter 自动延续无序列表、有序列表和任务列表；新任务默认未勾选，空列表项按 Enter 退出列表。
- Shift+Enter 仅插入换行；代码围栏和多行公式中 Enter 也仅插入换行。
- Live 模式中，代码块在非编辑状态将围栏行留空，点击后显示完整源码；围栏行始终保留高度，切换时排版不跳动。支持 C/C++、Python、JavaScript/TypeScript、JSON、Shell 和 LaTeX 的基础语法高亮；未标注或未知语言以等宽字体显示。

代码块右上角提供“复制代码”按钮，可将代码正文复制到剪贴板。

停止输入片刻后，修改会以 UTF-8 原子写回原文件。关闭窗口时会立即保存尚未写入的修改。也可通过命令行参数打开文件。支持 Markdown 标题、列表、表格、代码块和相对路径图片。

Live 编辑中，输入 `*`、`**`、`***`、`~~` 或 `==` 后，后续文字立即显示斜体、加粗、加粗斜体、删除线或高亮，直到配对标记闭合或当前行结束；代码与转义标记不触发样式。

正文支持 `==文字==` 浅黄色背景高亮，可与加粗、斜体和删除线组合；代码中的标记保持原样。

### 数学公式

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

使用内置 MicroTeX 与随程序部署的字体离线排版，不需要浏览器、网络或系统 LaTeX。代码块及行内代码不识别为公式。无法解析的公式保留源码。公式兼容范围以 MicroTeX 为准，不支持完整 LaTeX 文档或任意宏包。

### 表格编辑

原生 Markdown 表格可直接点击单元格编辑，支持中文输入法、复制/剪切/粘贴、加粗和斜体，修改同步回 Markdown 源码并可撤销。

- Tab / Shift+Tab 切换单元格，末格按 Tab 自动追加一行。
- 右键菜单可插入、移动和删除行列，至少保留一行一列；列移动保留对齐设置。
- 鼠标移到行左侧或列顶部边缘时显示手柄，点击选择整行/列，拖动调整顺序；选中后可复制、剪切或按 Delete 删除。
- 鼠标移到表格底部或右侧边缘时显示“＋”，用于追加行或列；“编辑”菜单也可插入新表格。

CSV 使用独立编辑面板，详见下方资源预览说明。

### 设置与查找

在“文件 → 设置”中调整基础字号（macOS 也可能位于应用设置菜单），快捷键 Ctrl/Cmd+,。字号即时作用于正文、源码、界面和行号，标题按层级比例缩放；提供“恢复默认值”。

查找采用字面文本匹配，支持区分大小写与循环查找；替换支持单次、全部替换，操作可撤销。快捷键在 macOS 上使用 Cmd，具体以菜单显示为准。

---

<a id="resources"></a>

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

---

<a id="privacy"></a>

## 隐私与数据

- **本地文件**：正文保存在用户打开的 Markdown 文件中，相对资源路径以该文件所在目录为基准。
- **保存方式**：停止输入片刻后自动写回文件，关闭窗口时立即保存尚未写入的修改；正文使用 UTF-8 原子写入。
- **资源加载**：资源预览仅加载本地文件，远程资源显示错误，不发起网络请求。
- **离线公式**：公式引擎与字体随程序部署，排版无需浏览器、网络或系统 LaTeX。
- **CSV 数据**：面板直接保存原 CSV，保存前检查外部修改；CSV 修改不进入 Markdown 正文的撤销历史。

---

<a id="development"></a>

## 开发与技术

技术栈为 **Qt 6 / C++17 / CMake / Ninja**，使用 **vcpkg** 管理依赖，公式排版采用 **MicroTeX**，PDF 阅读采用 **Qt PDF Widgets**。

👉 [开发指南：依赖、跨平台构建、VS Code 调试与测试](docs/development.md)

👉 [嵌入指南：在 Qt 应用中复用 MarkdownEditorWidget](docs/embedding.md)

👉 [综合测试文档与资源示例](综合测试.md)

当前 CTest 包含 10 项回归，覆盖编辑、表格、PDF、CSV、样式、行距、公式与嵌入组件。配置并构建后，可使用对应测试预设运行，例如：

```bash
ctest --preset macos-qt-debug
```

---

<a id="references"></a>

## 参考与依赖

- [Qt](https://www.qt.io/)：桌面界面、文档排版、图片与 PDF 支持。
- [MicroTeX](https://github.com/NanoMichael/MicroTeX)：离线数学公式排版，使用仓库内 overlay port 构建。
- [tinyxml2](https://github.com/leethomason/tinyxml2)：XML 解析依赖。
- [vcpkg](https://github.com/microsoft/vcpkg)：依赖管理与版本锁定。
- [DarkEye](https://github.com/gty5678/darkeye)：README 结构与排版参考。

依赖许可证随打包产物部署至 `licenses`；公式字体部署至 `res`，macOS 位于应用包的 `Contents/Resources` 下。

---

<p align="center">MdEdit — 让文字、公式与资源留在同一份文档里。</p>

[badge-qt]: https://img.shields.io/badge/Qt-6.12.0-41CD52?logo=qt&logoColor=white
[badge-cpp]: https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white
[badge-cmake]: https://img.shields.io/badge/CMake-3.24%2B-064F8C?logo=cmake&logoColor=white
[badge-platform]: https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-555555
[link-issues]: https://github.com/gty5678/MdEdit/issues
