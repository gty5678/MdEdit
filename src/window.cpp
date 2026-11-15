#include "window.h"

namespace {
bool writeDocument(const QString &path,const QByteArray &bytes){
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
}


Window::Window(){
        resize(980,780);updateTitle();
        connect(editingSurface,&MarkdownEditorWidget::fileLinkActivated,this,[this](const QString &file){
            if(!QFileInfo(file).isFile()){
                statusBar()->show();statusBar()->showMessage("链接文件不存在："+file,5000);return;
            }
            load(file);
        });
        auto *welcome=new QWidget;
        auto *layout=new QVBoxLayout(welcome);
        auto *open=new QPushButton("打开");
        open->setObjectName("openDocumentButton");
        open->setMinimumWidth(132);
        open->setFixedHeight(48);
        open->setCursor(Qt::PointingHandCursor);
        open->setStyleSheet(R"(
            QPushButton#openDocumentButton {
                background: #f2ebfb;
                color: #482775;
                border: 1px solid #e2d4f3;
                border-radius: 24px;
                font-weight: 600;
            }
            QPushButton#openDocumentButton:hover { background: #eae0f7; }
            QPushButton#openDocumentButton:pressed { background: #dfd0f1; }
            QPushButton#openDocumentButton:focus { border-color: #9877c2; }
        )");
        layout->addWidget(open,0,Qt::AlignCenter);
        auto *openShortcut=new QShortcut(QKeySequence::Open,welcome);
        openShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(openShortcut,&QShortcut::activated,open,&QPushButton::click);
        screens->addWidget(welcome);screens->addWidget(editingSurface);setCentralWidget(screens);
        documentStats->setObjectName("documentStats");
        documentStats->setToolTip("统计 Markdown 源码；字符数包含空白和标记，词数按 Unicode 词边界统计。");
        documentStats->setContentsMargins(8,2,8,2);
        statusBar()->addPermanentWidget(documentStats);
        statusBar()->setSizeGripEnabled(false);
        statusBar()->hide();
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
            connect(dialog,&QFileDialog::fileSelected,this,[this](const QString &file){load(file);});
            dialog->open();
        });
        auto *fileMenu=menuBar()->addMenu("文件");
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
        for(auto *action:editingSurface->editingActions())editMenu->addAction(action);
        auto *viewMenu=menuBar()->addMenu("视图");
        viewMenu->addAction(editingSurface->liveModeAction());
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
            if(settings.status()!=QSettings::NoError)statusBar()->showMessage("字号已生效，但设置保存失败");
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
        documentStats->setText(QString("%1 字符  ·  %2 词").arg(characters).arg(words));
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
            statusBar()->showMessage("自动保存失败，请检查文件权限和磁盘空间");statusBar()->show();return false;
        }
        editingSurface->document()->setModified(false);
        statusBar()->clearMessage();
        return true;
    }

void Window::load(const QString &file){
        prepareSwitch([this,file]{
        QFile f(file);
        if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        const auto bytes=f.readAll();
        if(f.error()!=QFileDevice::NoError){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        loading=true;documentOpen=true;
        path=QFileInfo(file).absoluteFilePath();
        editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
        editingSurface->setMarkdown(QString::fromUtf8(bytes));
        editingSurface->document()->setModified(false);
        autosave.stop();
        render();
        screens->setCurrentWidget(editingSurface);
        updateDocumentStats();
        statusBar()->show();
        editingSurface->setFocus();
        updateTitle();loading=false;rememberFile();
        });
    }

void Window::closeEvent(QCloseEvent *event){
    if(path.isEmpty()&&documentOpen&&editingSurface->document()->isModified()){
        event->ignore();prepareSwitch([this]{close();});return;
    }
    if(path.isEmpty()||save()){event->accept();return;}
    event->ignore();
    QMessageBox::warning(this,"无法关闭文档",statusBar()->currentMessage());
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
        editingSurface->setBaseUrl({});editingSurface->setMarkdown({});loading=false;
        screens->setCurrentWidget(editingSurface);statusBar()->show();
        updateDocumentStats();updateTitle();editingSurface->setFocus();
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
    if(!writeDocument(destination,bytes)){statusBar()->showMessage("另存为失败，请检查文件权限和磁盘空间");return false;}
    path=destination;autosave.stop();
    editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
    editingSurface->document()->setModified(false);rememberFile();updateTitle();
    statusBar()->showMessage("已保存："+path,5000);return true;
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
                refreshRecentFiles();statusBar()->show();statusBar()->showMessage("文件不存在，已从最近文件中移除："+file);return;
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
