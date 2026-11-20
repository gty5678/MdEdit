# LiveEditor 实现边界

`LiveEditor` 保持现有公开接口，继续以 Markdown 源码为准。`MarkdownEditorWidget` 的源码文档负责正文和撤销历史，Live 视图通过 `sourceChanged` 发布修改，通过 `history` 请求撤销或重做。

## 实现文件

| 文件 | 职责 |
| --- | --- |
| `src/editor/live_editor.cpp` | 初始化、信号连接、源码变化同步和资源刷新调度 |
| `src/editor/live/live_editor_layout.cpp` | 重建 Live 文档、组织源码区域和恢复视口 |
| `src/editor/live/live_editor_source.cpp` | 源码/显示位置转换、选区恢复、激活源码和编辑样式 |
| `src/editor/live/live_editor_render.cpp` | Markdown 片段渲染、渲染缓存、资源占位与行内图片适配 |
| `src/editor/live/live_editor_paint.cpp` | 视口绘制、代码和标题样式、图片尺寸适配 |
| `src/editor/live/live_editor_input.cpp` | 鼠标、键盘、焦点、粘贴、输入法和列表续写 |
| `src/editor/live/live_editor_tables.cpp` | 原生表格编辑、行列操作、手柄、表格移动和 CSV 摘要交互 |
| `src/editor/live/live_editor_resources.cpp` | 资源链接、资源操作按钮、引用源码编辑和 PDF 面板位置计算 |

这些文件是同一个类的实现单元，继续共享区域、活动编辑位置和表格交互状态。它们并非互相独立的控制器；拆分不改变编辑时序、源码映射算法或布局策略。

## PDF 组件

`src/editor/view/pdf_preview_panel.*` 中的 `PdfPreviewPanels` 独立管理 PDF 面板的创建、复用、隐藏与移除，以及文档生命周期、翻页工具栏和内外层滚动衔接。

Live 视图只提供面板的稳定键、文件路径、显示矩形和资源就绪状态。PDF 组件不读取或修改 Markdown 源码，不访问 Live 的区域和表格状态。

## 继续演进的边界

源码映射和表格状态仍在 `LiveEditor` 内维护；后续提取控制器时，需要明确源码替换与光标恢复的接口。当前拆分尚未引入增量解析、稳定 BlockId 或虚拟布局。

验证使用现有 CTest 回归，覆盖源码/Live 切换、撤销、输入样式、表格、PDF、CSV、公式和嵌入组件。

## 外部资源的视口窗口

Live 编辑器保留全文文字布局；图片、PDF、CSV 和文本附件只在「上一屏＋当前屏＋下一屏」范围内加载。滚动和视口变化合并调度，窗口外的资源块只保留尺寸和微小占位位图。本地图片通过读取头部尺寸计算高度；远程图片首次加载前使用默认比例，加载后保留尺寸信息供后续占位使用。公式仍由现有文字渲染流程处理。

窗口变化时同时清理资源快照、持有资源位图的 Markdown 片段缓存以及范围外 PDF 面板，并重建文档以释放其图片引用。网络请求在离开窗口后中止；排队的后台任务跳过读取，已经开始的解码任务完成后丢弃结果。仍在窗口内被其他位置引用的同一资源可以共享快照。回滚和选择范围沿用资源刷新时的视口锚定逻辑。

`resource-window` 测试覆盖禁止屏幕外加载、丢弃过期结果、缓存回收与重新请求，以及滚动前后的位图回收、全文高度和 Markdown 源码稳定性。
