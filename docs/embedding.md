# 嵌入 Markdown 编辑器

`MarkdownEditorWidget` 是普通 `QWidget`，可加入 layout、QSplitter、QTabWidget 或 QDockWidget。组件包含 Live/源码编辑、行号、语法高亮、公式、编辑命令和查找替换；不创建菜单栏、工具栏、文件对话框，不读写文件或持久化设置。

## 链接

依赖与主程序相同：C++17、Qt 6.12 Widgets/Svg 和 MicroTeX。使用现有工程的 CMake 配置后，宿主链接库目标：

```cmake
add_subdirectory(path/to/MdEdit mdedit)
target_link_libraries(MyApp PRIVATE MdEdit::Editor)
```

这个目标只链接编辑组件，不包含 `Window`、`main` 或应用测试。主程序目标也会被配置，可单独构建宿主目标。

MicroTeX 的初始化与释放由宿主管理。创建任何编辑器前调用 `tex::LaTeX::init(resourcePath.toStdString())`（需包含 `latex.h`），销毁所有编辑器后调用 `tex::LaTeX::release()`；整个进程只需初始化一次。将 `${MicroTeX_RESOURCE_DIR}` 的内容部署到宿主提供的资源目录，初始化时传入该目录的绝对路径。当前 MdEdit 主程序的部署与初始化已配置。

## 使用

```cpp
#include "editor/markdown_editor_widget.h"

auto *editor = new MarkdownEditorWidget(parent);
layout->addWidget(editor);
// 相对图片等资源相对于这个目录解析，目录 URL 以 / 结尾。
editor->setBaseUrl(QUrl::fromLocalFile("/path/to/documents/"));
editor->setMarkdown("# 标题\n\n可以直接编辑的 **Markdown**。");
editor->setLiveMode(true);
editor->applyBaseFontSize(14);

connect(editor, &MarkdownEditorWidget::textChanged, parent, [editor] {
    const QString text = editor->markdown();
    // 宿主决定保存或同步方式。
});
```

`setMarkdown()` 装载文本并重置选区、撤销历史和修改标记；它会发出文本变化通知，宿主装载文件时应避免把该通知当作用户修改立即保存。`document()` 返回权威源码文档，可查询 `isModified()`、连接修改通知，保存成功后调用 `setModified(false)`。`modificationChanged(bool)`、`liveModeChanged(bool)` 也由组件直接发出。

`setLiveMode()` 切换模式并保留源码选区；`selection()` / `selectSource()` 的位置是 QString 的 UTF-16 偏移。格式化、缩进、剪切复制及查找替换均有对应公开方法。

## 菜单与快捷键

组件自身不显示按钮。宿主可把同一组 QAction 加入自己的菜单：

```cpp
viewMenu->addAction(editor->liveModeAction());
for (auto *action : editor->editingActions())
    editMenu->addAction(action);
```

编辑快捷键使用 `Qt::WidgetWithChildrenShortcut`，仅在对应编辑器及其子控件获得焦点时生效，可在一个窗口中放置多个编辑器。QAction 归组件所有，宿主无需删除。设置菜单、设置持久化、打开/保存快捷键由宿主负责；修改字号直接调用 `applyBaseFontSize()`。
