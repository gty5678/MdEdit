# 嵌入 Markdown 编辑器

`MarkdownEditorWidget` 是普通 `QWidget`，可加入 layout、QSplitter、QTabWidget 或 QDockWidget。组件包含 Live/源码编辑、行号、语法高亮、公式、编辑命令和查找替换。宿主管理菜单栏、工具栏、文档读取和保存，以及设置持久化。

## 链接

依赖与主程序相同，包括 C++17、Qt 6.12（Widgets、Svg、Pdf、PdfWidgets、Network）和 MicroTeX。使用现有工程的 CMake 配置后，宿主可链接以下库目标。

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

## 内部链接

`insertWikiLink()` 打开可搜索的目标选择器，选择后写入 `[[引用|显示文字]]`。使用 `setProjectRoot()` 可将项目内的 Markdown 文件加入候选；仅设置本地目录 `baseUrl` 时，候选来自该目录。点击 Markdown 链接会发出 `fileLinkActivated(path)`，由宿主处理文档切换。

使用 `registerWikiLinkProvider()` 添加业务目标。每个来源使用独立前缀，引用中保存稳定标识，例如 `[[issue:4821|修复登录超时]]`。下面假设宿主的 `issues` 提供缓存搜索、按 ID 查找和打开工单的方法：

```cpp
editor->registerWikiLinkProvider("issue", {
    [issues](const QString &query, const WikiLinkContext &) {
        return issues->searchLinkTargets(query);
    },
    [issues](const QString &reference, const WikiLinkContext &) {
        return issues->resolveLinkTargets(reference.mid(6));
    },
    [issues](const WikiLinkTarget &target, const WikiLinkContext &) {
        issues->openIssue(target.reference.mid(6));
    }
});
```

`search` 和 `resolve` 返回 `QList<WikiLinkTarget>`。每项包含 `reference`、`title`、`detail` 和 `type`，例如 `{"issue:4821", "修复登录超时", "客户端", "工单"}`。空查询可返回初始候选。三个回调都在 UI 线程调用，搜索和解析应从宿主缓存返回。注册以编辑器实例为单位，同一前缀再次注册会替换原来源。

## 编辑数据库或 API 返回的正文

宿主用记录 ID 读取 Markdown 字段，把返回的 `QString` 传给 `setMarkdown()`。保存时调用 `markdown()` 取回 Markdown 源码，再写回同一条记录。整个流程可以在内存中完成。

以下示例让一个编辑器绑定一条记录。假设 `repository` 是宿主的数据访问对象，`row.markdown` 是已加载的正文，`saveAction` 是宿主的保存动作。

```cpp
// 传入空 QUrl，避免复用编辑器时沿用之前文档的资源基址。
editor->setMarkdown(row.markdown, QUrl{});

QObject::connect(saveAction, &QAction::triggered, editor,
                 [editor, &repository, id = row.id] {
    if (!editor->document()->isModified())
        return;

    if (repository.saveMarkdown(id, editor->markdown()))
        editor->document()->setModified(false);
    // 写入失败时由宿主提示，编辑器保留修改状态。
});
```

首次加载时也可以直接调用 `setMarkdown(row.markdown)`。如果同一个编辑器用于切换记录，保存处理器应读取宿主当前绑定的记录 ID。切换前由宿主处理未保存修改。若采用异步保存，请记录发出请求时的记录 ID 和正文快照，仅在请求成功且当前记录及正文仍与快照一致时清除修改标记。

正文与附件的来源可以独立配置。

- 若正文含有相对附件路径，给 `setMarkdown(text, baseUrl)` 传入相应的目录 URL，末尾带 `/`。本地目录可解析本地附件，HTTP(S) 基址可用于网络图片。
- 若正文含有其他数据库记录的内部链接，可通过 `registerWikiLinkProvider("note", provider)` 接管 `[[note:123]]`。宿主提供候选搜索、ID 解析和打开记录的回调。

## 菜单与快捷键

组件自身不显示按钮。宿主可把同一组 QAction 加入自己的菜单：

```cpp
viewMenu->addAction(editor->liveModeAction());
for (auto *action : editor->editingActions())
    editMenu->addAction(action);
```

编辑快捷键使用 `Qt::WidgetWithChildrenShortcut`，仅在对应编辑器及其子控件获得焦点时生效，可在一个窗口中放置多个编辑器。QAction 归组件所有，宿主无需删除。设置菜单、设置持久化、打开/保存快捷键由宿主负责；修改字号直接调用 `applyBaseFontSize()`。
