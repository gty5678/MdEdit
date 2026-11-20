#include "window.h"

#include <QDrag>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>

namespace {
bool writeDocument(const QString &path,const QByteArray &bytes){
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}

bool pathIsWithin(const QString &path,const QString &directory){
    if(path.isEmpty()||directory.isEmpty())return false;
    const QString relative=QDir(directory).relativeFilePath(path);
    return !QDir::isAbsolutePath(relative)&&relative!=".."&&!relative.startsWith("../");
}

QString relocatedPath(const QString &path,const QString &source,const QString &destination){
    if(path==source)return destination;
    return destination+"/"+QDir(source).relativeFilePath(path);
}

bool canMoveProjectEntry(const QFileInfo &entry,const QString &directory,const QString &root){
    const QString sourceParent=entry.dir().canonicalPath();
    const QString target=QFileInfo(directory).canonicalFilePath();
    return pathIsWithin(sourceParent,root)&&pathIsWithin(target,root)&&target!=sourceParent&&
        (entry.exists()||entry.isSymLink())&&
        !(entry.isDir()&&!entry.isSymLink()&&pathIsWithin(target,entry.canonicalFilePath()));
}

class ProjectFileTree final : public QTreeView {
public:
    explicit ProjectFileTree(QWidget *parent):QTreeView(parent){
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setIndentation(18);
        setIconSize(QSize(16,16));
        setUniformRowHeights(true);
        setMouseTracking(true);
        setTextElideMode(Qt::ElideMiddle);
    }

    std::function<void(const QString &,const QString &)> moveEntry;

protected:
    void drawBranches(QPainter *painter,const QRect &rect,const QModelIndex &index) const override {
        painter->fillRect(rect,QColor("#faf8fc"));
        if(!model()->hasChildren(index))return;
        const QPointF center(rect.right()-indentation()/2.0,rect.center().y());
        QPainterPath chevron;
        if(isExpanded(index)){
            chevron.moveTo(center+QPointF(-3,-1.5));
            chevron.lineTo(center+QPointF(0,1.5));
            chevron.lineTo(center+QPointF(3,-1.5));
        }else{
            chevron.moveTo(center+QPointF(-1.5,-3));
            chevron.lineTo(center+QPointF(1.5,0));
            chevron.lineTo(center+QPointF(-1.5,3));
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor("#807488"),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter->drawPath(chevron);
        painter->restore();
    }

    void startDrag(Qt::DropActions) override {
        const auto index=currentIndex();
        if(!index.isValid()||index==rootIndex())return;
        auto *files=static_cast<QFileSystemModel *>(model());
        auto *mime=new QMimeData;
        mime->setData(entryMimeType,files->filePath(index).toUtf8());
        auto *drag=new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(files->fileIcon(index).pixmap(24,24));
        drag->exec(Qt::MoveAction);
        stopAutoScroll();
        dropHighlight={};
        viewport()->update();
    }

    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->source()==this&&event->mimeData()->hasFormat(entryMimeType))event->acceptProposedAction();
        else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        const QPoint position=event->position().toPoint();
        const auto destination=dropDestination(event,position);
        if(hasAutoScroll()&&(position.y()<autoScrollMargin()||position.y()>=viewport()->height()-autoScrollMargin()))
            startAutoScroll();
        else stopAutoScroll();
        if(destination.isEmpty())dropHighlight={};
        else if(destination==static_cast<QFileSystemModel *>(model())->filePath(rootIndex()))
            dropHighlight=QRect(0,0,viewport()->width(),3);
        else dropHighlight=visualRect(indexAt(position));
        viewport()->update();
        if(destination.isEmpty())event->ignore();
        else event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override {
        dropHighlight={};
        viewport()->update();
        QTreeView::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override {
        stopAutoScroll();
        dropHighlight={};
        viewport()->update();
        const auto destination=dropDestination(event,event->position().toPoint());
        if(destination.isEmpty()){event->ignore();return;}
        event->acceptProposedAction();
        const QString source=QString::fromUtf8(event->mimeData()->data(entryMimeType));
        // Finish the drag before showing any filesystem error dialog.
        QTimer::singleShot(0,this,[this,source,destination]{moveEntry(source,destination);});
    }

    void paintEvent(QPaintEvent *event) override {
        QTreeView::paintEvent(event);
        if(dropHighlight.isEmpty())return;
        QPainter painter(viewport());
        painter.setPen(QColor("#9877c2"));
        painter.drawRect(dropHighlight.adjusted(0,0,-1,-1));
    }

private:
    static constexpr auto entryMimeType="application/x-mdedit-project-entry";
    QRect dropHighlight;

    QString dropDestination(const QDropEvent *event,const QPoint &position) const {
        if(event->source()!=this||!rootIndex().isValid())return {};
        auto *files=static_cast<QFileSystemModel *>(model());
        const auto index=indexAt(position);
        auto target=index.isValid()?(files->isDir(index)?index:index.parent()):rootIndex();
        // The upper edge of a top-level row is also a root drop target.
        if(index.isValid()&&index.parent()==rootIndex()&&position.y()-visualRect(index).top()<4)
            target=rootIndex();
        const QString source=QString::fromUtf8(event->mimeData()->data(entryMimeType));
        const QString directory=files->filePath(target);
        const QString root=QFileInfo(files->filePath(rootIndex())).canonicalFilePath();
        return canMoveProjectEntry(QFileInfo(source),directory,root)?directory:QString();
    }
};

QString applicationFolder(){
    const QString executableDirectory=QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    // Start beside the app bundle when running the packaged application.
    QDir directory(executableDirectory);
    if(directory.dirName()=="MacOS"&&directory.cdUp()&&directory.dirName()=="Contents"&&
       directory.cdUp()&&directory.dirName().endsWith(".app")&&directory.cdUp())
        return directory.absolutePath();
#endif
    return executableDirectory;
}

enum class ProjectIcon { Folder, File, AddFolder, AddFile, Collapse };

QIcon projectIcon(ProjectIcon iconType){
    const bool folder=iconType==ProjectIcon::Folder||iconType==ProjectIcon::AddFolder;
    const bool entry=iconType==ProjectIcon::Folder||iconType==ProjectIcon::File;
    QPainterPath outline;
    if(folder){
        outline.moveTo(1.5,12.5);outline.lineTo(1.5,3.5);outline.lineTo(5,3.5);
        outline.lineTo(6.5,5);outline.lineTo(14.5,5);outline.lineTo(14.5,12.5);outline.closeSubpath();
    }else if(iconType!=ProjectIcon::Collapse){
        outline.moveTo(3.5,14);outline.lineTo(3.5,2);outline.lineTo(10,2);
        outline.lineTo(13,5);outline.lineTo(13,14);outline.closeSubpath();
        outline.moveTo(10,2);outline.lineTo(10,5);outline.lineTo(13,5);
    }else{
        outline.moveTo(4,3.5);outline.lineTo(8,6.5);outline.lineTo(12,3.5);
        outline.moveTo(4,12.5);outline.lineTo(8,9.5);outline.lineTo(12,12.5);
    }
    if(iconType==ProjectIcon::AddFolder||iconType==ProjectIcon::AddFile){
        outline.moveTo(6,9);outline.lineTo(10,9);
        outline.moveTo(8,7);outline.lineTo(8,11);
    }
    QIcon icon;
    for(int scale:{1,2}){
        QPixmap pixmap(16*scale,16*scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(entry?(folder?"#94703d":"#7957a0"):"#644485"),
                            1.25,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(iconType==ProjectIcon::Folder)painter.setBrush(QColor("#f6e8ce"));
        painter.drawPath(outline);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

class ProjectFileIcons final : public QFileIconProvider {
public:
    QIcon icon(const QFileInfo &info) const override {
        return info.isDir()?folderIcon:fileIcon;
    }

private:
    const QIcon folderIcon=projectIcon(ProjectIcon::Folder);
    const QIcon fileIcon=projectIcon(ProjectIcon::File);
};

bool validProjectName(const QString &name){
    static const QRegularExpression invalidCharacters(R"([\\/:*?"<>|\x00-\x1f])");
    return !name.isEmpty()&&!name.startsWith('.')&&!name.contains(invalidCharacters);
}

QIcon projectFolderIcon()
{
    QPainterPath outline;
    outline.moveTo(3, 16);
    outline.lineTo(3, 5);
    outline.quadTo(3, 4, 4, 4);
    outline.lineTo(8, 4);
    outline.lineTo(10, 6);
    outline.lineTo(16, 6);
    outline.quadTo(17, 6, 17, 7);
    outline.lineTo(17, 8.5);
    outline.moveTo(3, 16);
    outline.lineTo(5.2, 9.5);
    outline.quadTo(5.5, 8.5, 6.5, 8.5);
    outline.lineTo(18.5, 8.5);
    outline.lineTo(16.5, 15.5);
    outline.quadTo(16.2, 16.5, 15.2, 16.5);
    outline.lineTo(4, 16.5);
    outline.quadTo(3, 16.5, 3, 16);

    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pixmap(20 * scale, 20 * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(outline);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
}


Window::Window(){
        resize(980,780);updateTitle();
        connect(editingSurface,&MarkdownEditorWidget::fileLinkActivated,this,[this](const QString &file){
            if(!QFileInfo(file).isFile()){
                showStatusMessage("链接文件不存在："+file,5000);return;
            }
            load(file);
        });
        auto *welcome=new QWidget;
        auto *layout=new QVBoxLayout(welcome);
        auto *actions=new QWidget(welcome);
        auto *actionsLayout=new QVBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(12);
        layout->addStretch();
        layout->addWidget(actions,0,Qt::AlignHCenter);
        layout->addStretch();

        auto *openFolderButton=new QPushButton("打开项目文件夹…");
        openFolderButton->setObjectName("openFolderButton");
        openFolderButton->setIcon(projectFolderIcon());
        openFolderButton->setIconSize(QSize(20, 20));
        openFolderButton->setMinimumWidth(220);
        openFolderButton->setMinimumHeight(52);
        openFolderButton->setCursor(Qt::PointingHandCursor);
        openFolderButton->setStyleSheet(R"(
            QPushButton#openFolderButton {
                background: #684396;
                color: #ffffff;
                border: 2px solid transparent;
                border-radius: 14px;
                padding: 6px 18px;
                font-weight: 600;
            }
            QPushButton#openFolderButton:hover { background: #583481; }
            QPushButton#openFolderButton:pressed { background: #482775; }
            QPushButton#openFolderButton:focus { border-color: #cbb1eb; }
        )");
        actionsLayout->addWidget(openFolderButton);

        auto *open=new QPushButton("打开 Markdown 文件…");
        open->setObjectName("openDocumentButton");
        open->setMinimumWidth(220);
        open->setMinimumHeight(48);
        open->setCursor(Qt::PointingHandCursor);
        open->setStyleSheet(R"(
            QPushButton#openDocumentButton {
                background: #f2ebfb;
                color: #482775;
                border: 1px solid #e2d4f3;
                border-radius: 14px;
                padding: 5px 18px;
                font-weight: 600;
            }
            QPushButton#openDocumentButton:hover { background: #eae0f7; }
            QPushButton#openDocumentButton:pressed { background: #dfd0f1; }
            QPushButton#openDocumentButton:focus { border-color: #9877c2; }
        )");
        actionsLayout->addWidget(open);
        auto *openShortcut=new QShortcut(QKeySequence::Open,welcome);
        openShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(openShortcut,&QShortcut::activated,open,&QPushButton::click);
        screens->addWidget(welcome);screens->addWidget(editingSurface);setCentralWidget(screens);
        floatingStatus=new QLabel(screens);
        floatingStatus->setObjectName("floatingStatus");
        floatingStatus->setTextFormat(Qt::PlainText);
        floatingStatus->setAttribute(Qt::WA_TransparentForMouseEvents);
        floatingStatus->setAccessibleDescription("统计 Markdown 源码的字符数和词数，并显示当前字号与操作提示。");
        floatingStatus->setStyleSheet(R"(
            QLabel#floatingStatus {
                background: #f8f4fc; color: #59496a;
                border: 1px solid #e4daee; border-radius: 13px;
                padding: 5px 11px; font-size: 12px;
            }
        )");
        floatingStatus->hide();
        screens->installEventFilter(this);
        statusTimer.setSingleShot(true);
        connect(&statusTimer,&QTimer::timeout,this,[this]{showStatusMessage({});});
        setStyleSheet(R"(QMainWindow,QWidget{background:#ffffff;color:#26232d;}QToolBar{background:#ffffff;border:0;border-bottom:1px solid #dedbe4;padding:8px;spacing:8px;}QToolButton{padding:6px 16px;border:0;border-radius:5px;}QToolButton:hover,QToolButton:checked{background:#eae2f7;color:#482775;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#e3d7f5;selection-color:#26232d;}QScrollBar:vertical{background:#ffffff;width:10px;}QScrollBar::handle:vertical{background:#a9a2b2;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#26232d;background:#f2eef7;})");
        autosave.setSingleShot(true);autosave.setInterval(600);
        statsTimer.setSingleShot(true);statsTimer.setInterval(250);
        connect(&statsTimer,&QTimer::timeout,this,[this]{updateDocumentStats();});
        connect(editingSurface,&MarkdownEditorWidget::textChanged,this,[this]{statsTimer.start();if(!loading&&documentOpen){
            if(!path.isEmpty())autosave.start();
        }});
        connect(editingSurface,&MarkdownEditorWidget::modificationChanged,this,[this]{updateTitle();});
        connect(&autosave,&QTimer::timeout,this,[this]{save();});
        connect(open,&QPushButton::clicked,this,[this]{
            auto *dialog=new QFileDialog(this,"打开 Markdown 文件",path,"Markdown (*.md *.markdown *.txt);;所有文件 (*)");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
            // Avoid Cocoa's nested native-panel modal loop (also under the debugger).
            dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
            dialog->setFileMode(QFileDialog::ExistingFile);
            connect(dialog,&QFileDialog::fileSelected,this,[this](const QString &file){load(file,true);});
            dialog->open();
        });
        auto chooseFolder=[this]{
            auto *dialog=new QFileDialog(this,"打开项目文件夹",applicationFolder());
            dialog->setObjectName("projectFolderDialog");
            dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setFileMode(QFileDialog::Directory);
            dialog->setOption(QFileDialog::ShowDirsOnly);
#ifdef Q_OS_MACOS
            dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
            connect(dialog,&QFileDialog::fileSelected,this,[this](const QString &folder){openFolder(folder);});dialog->open();
        };
        connect(openFolderButton,&QPushButton::clicked,this,chooseFolder);
        filesDock=new QDockWidget("文件",this);filesDock->setObjectName("filesDock");
        filesDock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
        filesDock->setStyleSheet(R"(
            QDockWidget#filesDock::title {
                background: #f1edf5; color: #51415f;
                text-align: left; padding: 7px 10px;
            }
            QWidget#projectFilesPanel, QWidget#projectFileActions { background: #faf8fc; }
            QTreeView#projectFileTree {
                background: #faf8fc; border: 0; outline: 0;
                selection-color: #4f2f74; show-decoration-selected: 0;
            }
            QTreeView#projectFileTree::item {
                padding: 4px 6px; min-height: 18px;
                margin: 1px 0; border: 1px solid transparent; border-radius: 6px;
            }
            QTreeView#projectFileTree::item:hover { background: #f1ebf7; }
            QTreeView#projectFileTree::item:selected {
                background: #eae0f5; border-color: #e4d6f1; color: #4f2f74;
            }
            QTreeView#projectFileTree QScrollBar:vertical {
                background: #faf8fc; width: 8px;
            }
            QTreeView#projectFileTree QScrollBar::handle:vertical { background: #c8bdd2; }
        )");
        auto *filesPanel=new QWidget(filesDock);
        filesPanel->setObjectName("projectFilesPanel");
        auto *filesLayout=new QVBoxLayout(filesPanel);
        filesLayout->setContentsMargins(8,6,8,6);
        filesLayout->setSpacing(4);

        auto *fileActions=new QWidget(filesPanel);
        fileActions->setObjectName("projectFileActions");
        auto *fileActionsLayout=new QHBoxLayout(fileActions);
        fileActionsLayout->setContentsMargins(1,0,1,4);
        fileActionsLayout->setSpacing(5);
        fileActions->setStyleSheet(R"(
            QToolButton {
                background: #f3edf8; border: 1px solid #ebe2f2;
                border-radius: 6px; padding: 4px;
            }
            QToolButton:hover:enabled { background: #eae2f7; }
            QToolButton:pressed { background: #ded0ef; }
            QToolButton:focus { border-color: #9877c2; }
            QToolButton:disabled { background: transparent; }
        )");
        auto addFileAction=[&](const QString &text,const QString &id,ProjectIcon icon){
            auto *button=new QToolButton(fileActions);
            button->setObjectName(id);
            button->setToolTip(text);
            button->setAccessibleName(text);
            button->setFocusPolicy(Qt::StrongFocus);
            button->setIconSize(QSize(16,16));
            button->setIcon(projectIcon(icon));
            fileActionsLayout->addWidget(button);
            return button;
        };
        auto *addFolderButton=addFileAction("添加文件夹","addProjectFolderButton",ProjectIcon::AddFolder);
        auto *newProjectFileButton=addFileAction("新建 MD 文件","newProjectFileButton",ProjectIcon::AddFile);
        auto *collapseFoldersButton=addFileAction("全部折叠","collapseFoldersButton",ProjectIcon::Collapse);
        fileActionsLayout->addStretch();
        filesLayout->addWidget(fileActions,0,Qt::AlignTop);

        auto *projectTree=new ProjectFileTree(filesPanel);
        fileTree=projectTree;fileTree->setObjectName("projectFileTree");
        projectTree->moveEntry=[this](const QString &source,const QString &destination){moveProjectEntry(source,destination);};
        filesLayout->addWidget(fileTree,1);
        fileModel=new QFileSystemModel(fileTree);fileModel->setReadOnly(true);
        fileModel->setIconProvider(new ProjectFileIcons);
        fileModel->setFilter(QDir::AllDirs|QDir::Files|QDir::NoDotAndDotDot);
        fileModel->setNameFilters({"*.md","*.markdown"});fileModel->setNameFilterDisables(false);
        fileTree->setModel(fileModel);fileTree->setHeaderHidden(true);fileTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int column=1;column<4;++column)fileTree->hideColumn(column);
        fileTree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        connect(fileTree,&QTreeView::clicked,this,[this](const QModelIndex &index){
            if(!fileModel->isDir(index))load(fileModel->filePath(index));
        });
        connect(addFolderButton,&QToolButton::clicked,this,[this]{
            const QString directory=selectedProjectFileDirectory();
            if(!directory.isEmpty())createProjectFolder(directory);
        });
        connect(newProjectFileButton,&QToolButton::clicked,this,[this]{
            const QString directory=selectedProjectFileDirectory();
            if(!directory.isEmpty())createProjectFile(directory);
        });
        connect(collapseFoldersButton,&QToolButton::clicked,fileTree,&QTreeView::collapseAll);
        fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(fileTree,&QTreeView::customContextMenuRequested,this,[this](const QPoint &position){
            const auto index=fileTree->indexAt(position);
            const QString directory=projectFileDirectory(index);
            if(directory.isEmpty())return;
            QMenu menu(fileTree);
            auto *create=menu.addAction("新建 Markdown 文件…");
            QAction *remove=nullptr;
            QAction *rename=nullptr;
            const QString entry=fileModel->filePath(index);
            if(index.isValid()&&index!=fileTree->rootIndex()){
                menu.addSeparator();
                rename=menu.addAction("重命名…");
                remove=menu.addAction("删除…");
            }
            const auto chosen=menu.exec(fileTree->viewport()->mapToGlobal(position));
            if(chosen==create)createProjectFile(directory);
            else if(chosen&&chosen==rename)renameProjectEntry(entry);
            else if(chosen&&chosen==remove)deleteProjectEntry(entry);
        });
        connect(fileModel,&QFileSystemModel::directoryLoaded,this,[this]{
            const auto index=fileModel->index(path);if(index.isValid())fileTree->setCurrentIndex(index);
        });
        connect(fileModel,&QFileSystemModel::rowsInserted,this,[this](const QModelIndex &parent,int first,int last){
            if(path.isEmpty()||fileModel->filePath(parent)!=QFileInfo(path).absolutePath())return;
            for(int row=first;row<=last;++row){
                const auto index=fileModel->index(row,0,parent);
                if(fileModel->filePath(index)!=path)continue;
                if(parent!=fileTree->rootIndex())fileTree->expand(parent);
                fileTree->setCurrentIndex(index);
                fileTree->scrollTo(index);
                break;
            }
        });
        filesDock->setWidget(filesPanel);addDockWidget(Qt::LeftDockWidgetArea,filesDock);
        refreshFileTree();
        resizeDocks({filesDock},{230},Qt::Horizontal);
        auto *fileMenu=menuBar()->addMenu("文件");
        auto *folderAction=fileMenu->addAction("打开文件夹…");folderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
        connect(folderAction,&QAction::triggered,this,chooseFolder);
        auto *closeFolder=fileMenu->addAction("关闭项目文件夹");
        connect(closeFolder,&QAction::triggered,this,[this]{projectRoot.clear();editingSurface->setProjectRoot({});refreshFileTree();});
        auto *newAction=fileMenu->addAction("新建");newAction->setShortcut(QKeySequence::New);
        connect(newAction,&QAction::triggered,this,[this]{newDocument();});
        auto *openAction=fileMenu->addAction("打开…");openAction->setShortcut(QKeySequence::Open);
        connect(openAction,&QAction::triggered,open,&QPushButton::click);
        // The window action also works after leaving the welcome screen.
        openShortcut->setEnabled(false);
        auto *saveAction=fileMenu->addAction("保存");saveAction->setShortcut(QKeySequence::Save);
        connect(saveAction,&QAction::triggered,this,[this]{save();});
        auto *saveAsAction=fileMenu->addAction("另存为…");saveAsAction->setShortcut(QKeySequence::SaveAs);
        connect(saveAsAction,&QAction::triggered,this,[this]{chooseSaveAs();});
        recentMenu=fileMenu->addMenu("最近文件");refreshRecentFiles();
        auto *editMenu=menuBar()->addMenu("编辑");
        for(auto *action:editingSurface->editingActions()){
            editMenu->addAction(action);
            if(action->objectName()=="insertWikiLinkAction"){
                const auto update=[this,action]{action->setEnabled(screens->currentWidget()==editingSurface);};
                connect(screens,&QStackedWidget::currentChanged,action,update);
                update();
            }
        }
        auto *viewMenu=menuBar()->addMenu("视图");
        viewMenu->addAction(editingSurface->liveModeAction());
        viewMenu->addAction(filesDock->toggleViewAction());
        auto *settingsAction=new QAction("设置",this);
        settingsAction->setObjectName("settingsAction");settingsAction->setShortcut(QKeySequence("Ctrl+,"));
        settingsAction->setMenuRole(QAction::PreferencesRole);
        fileMenu->addSeparator();fileMenu->addAction(settingsAction);
        connect(settingsAction,&QAction::triggered,this,[this]{showSettings();});
        applyBaseFontSize(QSettings().value("appearance/baseFontSize",14).toInt());
        editingSurface->document()->setModified(false);
        render();open->setFocus();
    }

void Window::applyBaseFontSize(int size){
        baseFontSize=qBound(9,size,30);
        QFont ui=defaultUiFont;
        ui.setPointSizeF(qMax(9.0,(ui.pointSizeF()>0?ui.pointSizeF():13)+baseFontSize-13));
        setFont(ui);menuBar()->setFont(ui);
        editingSurface->applyBaseFontSize(baseFontSize);
        updateFloatingStatus();
    }

void Window::showSettings(){
        if(auto *existing=findChild<QDialog *>("settingsDialog")){existing->show();existing->raise();existing->activateWindow();return;}
        auto *dialog=new QDialog(this);dialog->setObjectName("settingsDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("设置");
        auto *layout=new QFormLayout(dialog);
        auto *size=new QSpinBox;size->setObjectName("baseFontSize");size->setRange(9,30);size->setSuffix(" pt");size->setValue(baseFontSize);
        layout->addRow("基础字号",size);
        auto *hint=new QLabel("调整正文、源码和界面字号；标题按比例缩放。修改即时生效并自动保存。");
        hint->setWordWrap(true);layout->addRow(hint);
        connect(size,&QSpinBox::valueChanged,this,[this](int value){
            applyBaseFontSize(value);QSettings settings;settings.setValue("appearance/baseFontSize",baseFontSize);settings.sync();
            if(settings.status()!=QSettings::NoError)showStatusMessage("字号已生效，但设置保存失败");
        });
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::RestoreDefaults|QDialogButtonBox::Close);layout->addRow(buttons);
        connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,dialog,[size]{size->setValue(14);});
        connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
        dialog->open();size->setFocus();
    }

QPair<int,int> Window::selection() const{return editingSurface->selection();}
void Window::selectSource(int a,int b){editingSurface->selectSource(a,b);}
void Window::replaceSource(int a,int b,const QString &text,int anchor,int position){editingSurface->replaceSource(a,b,text,anchor,position);}
void Window::formatSelection(const QString &left,const QString &right){editingSurface->formatSelection(left,right);}
void Window::clipboardParagraph(bool cut){editingSurface->clipboardParagraph(cut);}
void Window::indentSelection(bool outdent){editingSurface->indentSelection(outdent);}
void Window::showFind(bool replace){editingSurface->showFind(replace);}

void Window::updateDocumentStats(){
        const QString text=editingSurface->markdown();
        qsizetype characters=0,words=0;
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,text);
        while(graphemes.toNextBoundary()!=-1)++characters;
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Word,text);
        do{
            if(boundaries.boundaryReasons()&QTextBoundaryFinder::StartOfItem)++words;
        }while(boundaries.toNextBoundary()!=-1);
        documentStats=QString("%1 字符  ·  %2 词").arg(characters).arg(words);
        updateFloatingStatus();
    }

void Window::showStatusMessage(const QString &message,int timeout){
    statusTimer.stop();
    statusMessage=message;
    if(timeout>0)statusTimer.start(timeout);
    updateFloatingStatus();
}

void Window::updateFloatingStatus(){
    const QString separator="  ·  ";
    QString summary;
    if(documentOpen)
        summary=documentStats+separator+QString("字号 %1 pt").arg(baseFontSize);
    if(statusMessage.isEmpty()&&summary.isEmpty()){
        floatingStatus->hide();
        return;
    }

    constexpr int rightInset=18,bottomInset=12,leftInset=12,horizontalChrome=24;
    const int availableWidth=qMax(0,screens->width()-rightInset-leftInset-horizontalChrome);
    floatingStatus->ensurePolished();
    const QFontMetrics metrics(floatingStatus->font());
    QString display;
    if(statusMessage.isEmpty()||summary.isEmpty()){
        display=metrics.elidedText(statusMessage+summary,Qt::ElideRight,availableWidth);
    }else{
        const int messageWidth=availableWidth-metrics.horizontalAdvance(separator+summary);
        display=metrics.elidedText(statusMessage,Qt::ElideRight,availableWidth);
        if(messageWidth>=metrics.horizontalAdvance("……")){
            display=metrics.elidedText(statusMessage,Qt::ElideRight,messageWidth)+separator+summary;
        }
    }
    floatingStatus->setText(display);
    floatingStatus->setAccessibleName(statusMessage.isEmpty()||summary.isEmpty()
        ?statusMessage+summary:statusMessage+separator+summary);
    floatingStatus->resize(floatingStatus->sizeHint());
    floatingStatus->move(screens->width()-rightInset-floatingStatus->width(),
                         screens->height()-bottomInset-floatingStatus->height());
    floatingStatus->setVisible(!display.isEmpty());
    floatingStatus->raise();
}

bool Window::eventFilter(QObject *watched,QEvent *event){
    if(watched==screens&&event->type()==QEvent::Resize)updateFloatingStatus();
    return QMainWindow::eventFilter(watched,event);
}

void Window::updateTitle(){
        const bool modified=editingSurface->document()->isModified();
        if(path.isEmpty()){setWindowTitle(documentOpen?QString("未命名")+(modified?" *":"")+" — MdEdit":"MdEdit");return;}
        const QString name=QFileInfo(path).fileName();
        setWindowTitle(name+(modified?" *":"")+" — MdEdit");
    }

void Window::render(){editingSurface->render();}

bool Window::save(){
        if(path.isEmpty()){
            if(documentOpen){chooseSaveAs();return false;}
            return true;
        }
        if(!editingSurface->document()->isModified())return true;
        const auto bytes=editingSurface->markdown().toUtf8();
        if(!writeDocument(path,bytes)){
            showStatusMessage("自动保存失败，请检查文件权限和磁盘空间");return false;
        }
        editingSurface->document()->setModified(false);
        showStatusMessage({});
        return true;
    }

void Window::load(const QString &file,bool standalone){
        if(QFileInfo(file).isDir()){openFolder(file);return;}
        prepareSwitch([this,file,standalone]{
        QFile f(file);
        if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        const auto bytes=f.readAll();
        if(f.error()!=QFileDevice::NoError){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        if(standalone)projectRoot.clear();
        showDocument(file,QString::fromUtf8(bytes));
        });
    }

void Window::showDocument(const QString &file,const QString &markdown){
    editingSurface->setProjectRoot(projectRoot);
    loading=true;documentOpen=true;
    path=QFileInfo(file).absoluteFilePath();
    editingSurface->setMarkdown(markdown,QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
    editingSurface->document()->setModified(false);
    autosave.stop();
    screens->setCurrentWidget(editingSurface);
    updateDocumentStats();
    showStatusMessage({});
    editingSurface->setFocus();
    updateTitle();loading=false;rememberFile();refreshFileTree();
}

void Window::moveProjectEntry(const QString &source,const QString &directory){
    const QFileInfo entry(source);
    const QString root=QFileInfo(fileModel->filePath(fileTree->rootIndex())).canonicalFilePath();
    if(!canMoveProjectEntry(entry,directory,root))return;

    const QString destination=QDir(directory).filePath(entry.fileName());
    const QFileInfo existing(destination);
    if(existing.exists()||existing.isSymLink()){
        QMessageBox::warning(this,"无法移动","目标文件夹中已有同名项目。");
        return;
    }
    const bool movingDocument=pathIsWithin(path,source);
    if(movingDocument&&!save())return;
    if(!QDir().rename(source,destination)){
        QMessageBox::warning(this,"无法移动","请检查文件夹权限和目标位置。");
        return;
    }
    updateRecentPaths(source,destination);
    if(movingDocument){
        path=relocatedPath(path,source,destination);
        autosave.stop();
        editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
        updateTitle();
    }
    const auto target=fileModel->index(directory);
    if(target!=fileTree->rootIndex())fileTree->expand(target);
    if(movingDocument&&!projectRoot.isEmpty())refreshFileTree();
}

void Window::renameProjectEntry(const QString &entry){
    const QFileInfo info(entry);
    const QString treeRoot=fileModel->filePath(fileTree->rootIndex());
    if(!pathIsWithin(info.dir().canonicalPath(),QFileInfo(treeRoot).canonicalFilePath()))return;
    auto *dialog=new QInputDialog(this);
    dialog->setObjectName("renameProjectEntryDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("重命名");
    dialog->setLabelText(info.isDir()?"新文件夹名称：":"新名称（包含文件扩展名）：");
    dialog->setTextValue(info.fileName());
    connect(dialog,&QInputDialog::textValueSelected,this,[this,entry](const QString &input){
        const QString name=input.trimmed();
        if(!validProjectName(name)){
            QMessageBox::warning(this,"无法重命名","请输入有效的名称。");
            return;
        }
        const QString destination=QFileInfo(entry).dir().filePath(name);
        if(destination==entry)return;
        const QFileInfo existing(destination);
        if(existing.exists()||existing.isSymLink()){
            QMessageBox::warning(this,"无法重命名","已有同名文件或文件夹。");
            return;
        }
        const bool renamingDocument=pathIsWithin(path,entry);
        if(renamingDocument&&!save())return;
        if(!QDir().rename(entry,destination)){
            QMessageBox::warning(this,"无法重命名","请检查名称和文件夹权限。");
            return;
        }
        updateRecentPaths(entry,destination);
        if(renamingDocument){
            path=relocatedPath(path,entry,destination);
            autosave.stop();
            editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
            updateTitle();
        }
        refreshFileTree();
        fileTree->setCurrentIndex(fileModel->index(destination));
    });
    dialog->open();
}

void Window::deleteProjectEntry(const QString &entry){
    const QFileInfo info(entry);
    const QString treeRoot=fileModel->filePath(fileTree->rootIndex());
    if(!pathIsWithin(info.dir().canonicalPath(),QFileInfo(treeRoot).canonicalFilePath()))return;
    const QString question=info.isDir()&&!info.isSymLink()
        ?QString("将文件夹「%1」及其全部内容移到废纸篓？").arg(info.fileName())
        :QString("将「%1」移到废纸篓？").arg(info.fileName());
    if(QMessageBox::question(this,"删除",question,QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)
        return;
    const bool deletingDocument=pathIsWithin(path,entry);
    if(deletingDocument&&!save())return;
    QFile file(entry);
    if(!file.moveToTrash()){
        QMessageBox::warning(this,"无法移到废纸篓",file.errorString());
        return;
    }
    updateRecentPaths(entry,{});
    if(deletingDocument)openFolder(treeRoot);
}

void Window::updateRecentPaths(const QString &source,const QString &destination){
    QSettings settings;
    QStringList recent;
    for(const auto &file:settings.value("files/recent").toStringList()){
        if(!pathIsWithin(file,source))recent.append(file);
        else if(!destination.isEmpty())recent.append(relocatedPath(file,source,destination));
    }
    recent.removeDuplicates();
    settings.setValue("files/recent",recent);
    refreshRecentFiles();
}

void Window::createProjectFolder(const QString &directory){
    auto *dialog=new QInputDialog(this);
    dialog->setObjectName("newProjectFolderDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("添加文件夹");
    dialog->setLabelText(QString("在 %1 中新建文件夹").arg(QDir::toNativeSeparators(directory)));
    dialog->setTextValue("新建文件夹");
    connect(dialog,&QInputDialog::textValueSelected,this,[this,directory](const QString &input){
        const QString name=input.trimmed();
        if(!validProjectName(name)){
            QMessageBox::warning(this,"无法添加文件夹","请输入有效的文件夹名称。");
            return;
        }
        if(!QDir(directory).mkdir(name)){
            QMessageBox::warning(this,"无法添加文件夹","请检查名称是否已被使用以及目录是否可写。");
            return;
        }
        const auto parent=fileModel->index(directory);
        if(parent!=fileTree->rootIndex())fileTree->expand(parent);
    });
    dialog->open();
}

void Window::createProjectFile(const QString &directory){
    auto *dialog=new QInputDialog(this);
    dialog->setObjectName("newMarkdownFileDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("新建 Markdown 文件");
    dialog->setLabelText(QString("在 %1 中新建文件").arg(QDir::toNativeSeparators(directory)));
    dialog->setTextValue("未命名.md");
    connect(dialog,&QInputDialog::textValueSelected,this,[this,directory](const QString &input){
        QString name=input.trimmed();
        if(!validProjectName(name)){
            QMessageBox::warning(this,"无法新建文件","请输入有效的文件名。");
            return;
        }
        if(!name.endsWith(".md",Qt::CaseInsensitive))name+=".md";
        const QString destination=QDir(directory).filePath(name);
        const bool modifiedUntitled=path.isEmpty()&&editingSurface->document()->isModified();
        prepareSwitch([this,destination,modifiedUntitled]{
            QFile file(destination);
            if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){
                if(modifiedUntitled&&path.isEmpty())editingSurface->document()->setModified(true);
                QMessageBox::warning(this,"无法新建文件",file.errorString());
                return;
            }
            file.close();
            showDocument(destination,{});
        });
    });
    dialog->open();
}

void Window::closeEvent(QCloseEvent *event){
    if(path.isEmpty()&&documentOpen&&editingSurface->document()->isModified()){
        event->ignore();prepareSwitch([this]{close();});return;
    }
    if(path.isEmpty()||save()){event->accept();return;}
    event->ignore();
    QMessageBox::warning(this,"无法关闭文档",statusMessage);
}

void Window::prepareSwitch(std::function<void()> next){
    if(!editingSurface->document()->isModified()){next();return;}
    if(!path.isEmpty()){if(save())next();return;}
    auto *dialog=new QMessageBox(QMessageBox::Question,"保存未命名文档？","当前文档有未保存的修改。",
        QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,this);
    dialog->setObjectName("unsavedDocumentDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setDefaultButton(QMessageBox::Save);
    connect(dialog,&QMessageBox::finished,this,[this,next](int result){
        if(result==QMessageBox::Save)chooseSaveAs(next);
        else if(result==QMessageBox::Discard){
            editingSurface->document()->setModified(false);next();
        }
    });dialog->open();
}

void Window::newDocument(){
    prepareSwitch([this]{
        autosave.stop();loading=true;path.clear();documentOpen=true;
        editingSurface->setMarkdown({},projectRoot.isEmpty()?QUrl():QUrl::fromLocalFile(projectRoot+"/"));loading=false;
        screens->setCurrentWidget(editingSurface);
        updateDocumentStats();showStatusMessage({});updateTitle();editingSurface->setFocus();
    });
}

void Window::chooseSaveAs(std::function<void()> next){
    if(!documentOpen)return;
    if(auto *existing=findChild<QFileDialog *>("saveAsDialog")){existing->raise();return;}
    auto *dialog=new QFileDialog(this,"另存为",path.isEmpty()?QDir::homePath()+"/未命名.md":path,"Markdown (*.md *.markdown);;所有文件 (*)");
    dialog->setObjectName("saveAsDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setDefaultSuffix("md");
#ifdef Q_OS_MACOS
    dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
    connect(dialog,&QFileDialog::fileSelected,this,[this,next](const QString &file){if(saveAs(file)&&next)next();});
    dialog->open();
}

bool Window::saveAs(const QString &target){
    if(!documentOpen||target.isEmpty())return false;
    const QString destination=QFileInfo(target).absoluteFilePath();
    if(destination==path||(!QFileInfo(target).canonicalFilePath().isEmpty()&&QFileInfo(target).canonicalFilePath()==QFileInfo(path).canonicalFilePath()))return save();
    const auto bytes=editingSurface->markdown().toUtf8();
    if(!writeDocument(destination,bytes)){showStatusMessage("另存为失败，请检查文件权限和磁盘空间");return false;}
    path=destination;autosave.stop();
    editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
    editingSurface->document()->setModified(false);rememberFile();updateTitle();refreshFileTree();
    showStatusMessage("已保存："+path,5000);return true;
}

void Window::rememberFile(){
    if(path.isEmpty())return;
    QSettings settings;auto recent=settings.value("files/recent").toStringList();
    recent.removeAll(path);recent.prepend(path);while(recent.size()>10)recent.removeLast();
    settings.setValue("files/recent",recent);refreshRecentFiles();
}
void Window::refreshRecentFiles(){
    recentMenu->clear();const auto recent=QSettings().value("files/recent").toStringList();
    for(const auto &file:recent){
        auto *action=recentMenu->addAction(file);action->setToolTip(file);
        connect(action,&QAction::triggered,this,[this,file]{
            if(!QFile::exists(file)){
                auto entries=QSettings().value("files/recent").toStringList();entries.removeAll(file);QSettings().setValue("files/recent",entries);
                refreshRecentFiles();showStatusMessage("文件不存在，已从最近文件中移除："+file);return;
            }
            load(file);
        });
    }
    if(recent.isEmpty()){auto *empty=recentMenu->addAction("暂无最近文件");empty->setEnabled(false);}
    else{
        recentMenu->addSeparator();auto *clear=recentMenu->addAction("清空最近文件");
        connect(clear,&QAction::triggered,this,[this]{QSettings().remove("files/recent");refreshRecentFiles();});
    }
}

void Window::openFolder(const QString &folder){
    const QFileInfo info(folder);if(!info.isDir())return;
    prepareSwitch([this,folder]{
        autosave.stop();projectRoot=QFileInfo(folder).absoluteFilePath();path.clear();documentOpen=false;loading=true;
        editingSurface->setProjectRoot(projectRoot);
        editingSurface->setMarkdown({},QUrl::fromLocalFile(projectRoot+"/"));
        editingSurface->document()->setModified(false);loading=false;
        screens->setCurrentIndex(0);updateTitle();refreshFileTree();
        documentStats.clear();showStatusMessage("已打开项目："+projectRoot,5000);
    });
}
QString Window::projectFileDirectory(const QModelIndex &index) const{
    if(!index.isValid())return fileModel->filePath(fileTree->rootIndex());
    return fileModel->filePath(fileModel->isDir(index)?index:index.parent());
}

QString Window::selectedProjectFileDirectory() const{
    const auto index=fileTree->currentIndex();
    if(fileTree->selectionModel()->isSelected(index)&&fileTree->visualRect(index).isValid())
        return projectFileDirectory(index);
    return projectFileDirectory({});
}

void Window::refreshFileTree(){
    filesDock->toggleViewAction()->setEnabled(!projectRoot.isEmpty());
    if(projectRoot.isEmpty()){
        filesDock->hide();
        fileTree->clearSelection();
        fileTree->setRootIndex({});
        filesDock->setWindowTitle("文件");
        return;
    }
    fileTree->setRootIndex(fileModel->setRootPath(projectRoot));
    filesDock->setWindowTitle("项目 · "+QDir(projectRoot).dirName());filesDock->show();
    const auto index=fileModel->index(path);
    if(index.isValid()){
        for(auto parent=index.parent();parent.isValid()&&parent!=fileTree->rootIndex();parent=parent.parent())fileTree->expand(parent);
        fileTree->setCurrentIndex(index);fileTree->scrollTo(index);
    }
}
