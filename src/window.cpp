#include "window.h"
#include "editor/highlighter.h"

Window::Window(){
        resize(980,780);updateTitle();
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
        bar=addToolBar("文档");bar->setMovable(false);bar->setContextMenuPolicy(Qt::PreventContextMenu);
        bar->hide();
        auto *spacer=new QWidget;spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);bar->addWidget(spacer);
        previewAction=bar->addAction("Live 编辑");previewAction->setCheckable(true);previewAction->setChecked(true);previewAction->setShortcut(QKeySequence("Ctrl+E"));
        previewAction->setObjectName("livePreviewAction");editor->setObjectName("markdownEditor");preview->setObjectName("markdownPreview");
        editor->setFrameShape(QFrame::NoFrame);editor->setFont(QFont(editorFont,14));editor->setTabStopDistance(32);editor->document()->setDocumentMargin(32);new Highlighter(editor->document());
        preview->setFrameShape(QFrame::NoFrame);preview->setFont(QFont(previewFont,13));preview->document()->setDocumentMargin(40);preview->setOpenLinks(false);
        pages->addWidget(editor);pages->addWidget(preview);pages->setCurrentIndex(1);
        screens->addWidget(welcome);screens->addWidget(pages);setCentralWidget(screens);
        documentStats->setObjectName("documentStats");
        documentStats->setToolTip("统计 Markdown 源码；字符数包含空白和标记，词数按 Unicode 词边界统计。");
        documentStats->setContentsMargins(8,2,8,2);
        statusBar()->addPermanentWidget(documentStats);
        statusBar()->setSizeGripEnabled(false);
        statusBar()->hide();
        setStyleSheet(R"(QMainWindow,QWidget{background:#ffffff;color:#26232d;}QToolBar{background:#ffffff;border:0;border-bottom:1px solid #dedbe4;padding:8px;spacing:8px;}QToolButton{padding:6px 16px;border:0;border-radius:5px;}QToolButton:hover,QToolButton:checked{background:#eae2f7;color:#482775;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#e3d7f5;selection-color:#26232d;}QScrollBar:vertical{background:#ffffff;width:10px;}QScrollBar::handle:vertical{background:#a9a2b2;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#26232d;background:#f2eef7;})");
        timer.setSingleShot(true);timer.setInterval(150);
        autosave.setSingleShot(true);autosave.setInterval(600);
        statsTimer.setSingleShot(true);statsTimer.setInterval(250);
        connect(&statsTimer,&QTimer::timeout,this,[this]{updateDocumentStats();});
        preview->sourceChanged=[this](const QString &text){
            const QString before=editor->toPlainText();int begin=0,oldEnd=before.size(),newEnd=text.size();
            while(begin<oldEnd&&begin<newEnd&&before[begin]==text[begin])++begin;
            while(oldEnd>begin&&newEnd>begin&&before[oldEnd-1]==text[newEnd-1]){--oldEnd;--newEnd;}
            if(begin==oldEnd&&begin==newEnd)return;
            QTextCursor c(editor->document());c.setPosition(begin);c.setPosition(oldEnd,QTextCursor::KeepAnchor);
            c.insertText(text.mid(begin,newEnd-begin));
        };
        preview->history=[this](bool redo){if(redo)editor->redo();else editor->undo();render();};
        connect(editor,&QPlainTextEdit::textChanged,this,[this]{
            statsTimer.start();
            timer.start();
            if(!path.isEmpty())autosave.start();
        });
        connect(&autosave,&QTimer::timeout,this,[this]{save();});
        connect(editor->document(),&QTextDocument::modificationChanged,this,[this]{updateTitle();});
        connect(&timer,&QTimer::timeout,this,[this]{if(previewAction->isChecked())render();});
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
        connect(previewAction,&QAction::toggled,this,[this](bool enabled){
            if(enabled){
                const auto caret=editor->textCursor();render();
                preview->restoreSourceSelection(caret.anchor(),caret.position());
            }else{
                const auto selection=preview->sourceSelection();
                QTextCursor caret(editor->document());caret.setPosition(selection.first);
                caret.setPosition(selection.second,QTextCursor::KeepAnchor);editor->setTextCursor(caret);
            }
            pages->setCurrentIndex(enabled?1:0);previewAction->setText(enabled?"Live 编辑":"源码");
            if(enabled)preview->setFocus();else{editor->setFocus();editor->ensureCursorVisible();}
        });
        connect(preview,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){
            if(url.scheme()=="https"||url.scheme()=="http")QDesktopServices::openUrl(url);
            else if(url.isRelative()&&url.path().isEmpty()&&!url.fragment().isEmpty())preview->scrollToAnchor(url.fragment());
        });
        auto *fileMenu=menuBar()->addMenu("文件");
        auto *openAction=fileMenu->addAction("打开…");openAction->setShortcut(QKeySequence::Open);
        connect(openAction,&QAction::triggered,open,&QPushButton::click);
        // The window action also works after leaving the welcome screen.
        openShortcut->setEnabled(false);
        auto *saveAction=fileMenu->addAction("保存");saveAction->setShortcut(QKeySequence::Save);
        connect(saveAction,&QAction::triggered,this,[this]{save();});
        auto *editMenu=menuBar()->addMenu("编辑");
        auto command=[this,editMenu](const QString &name,const QKeySequence &key,std::function<void()> fn){
            auto *action=editMenu->addAction(name);action->setShortcut(key);
            connect(action,&QAction::triggered,this,[this,fn]{if(screens->currentWidget()==pages)fn();});
        };
        command("加粗",QKeySequence::Bold,[this]{formatSelection("**","**");});
        command("斜体",QKeySequence::Italic,[this]{formatSelection("*","*");});
        command("行内代码",QKeySequence("Ctrl+Shift+C"),[this]{formatSelection("`","`");});
        command("插入链接",QKeySequence("Ctrl+K"),[this]{formatSelection("[","](url)");});
        command("复制段落 / 选区",QKeySequence::Copy,[this]{clipboardParagraph(false);});
        command("剪切段落 / 选区",QKeySequence::Cut,[this]{clipboardParagraph(true);});
        command("增加缩进",QKeySequence("Ctrl+]"),[this]{indentSelection(false);});
        command("减少缩进",QKeySequence("Ctrl+["),[this]{indentSelection(true);});
        command("查找…",QKeySequence::Find,[this]{showFind(false);});
        command("替换…",QKeySequence("Ctrl+H"),[this]{showFind(true);});
        command("删除当前行",QKeySequence("Ctrl+Shift+K"),[this]{
            const auto r=selection();if(r.first!=r.second)return;
            const auto text=editor->toPlainText();int a=r.first?text.lastIndexOf('\n',r.first-1)+1:0;
            int b=text.indexOf('\n',r.first);if(b<0){b=text.size();if(a>0)--a;}else ++b;
            replaceSource(a,b,{},a,a);
        });
        auto *settingsAction=bar->addAction("设置");
        settingsAction->setObjectName("settingsAction");settingsAction->setShortcut(QKeySequence("Ctrl+,"));
        settingsAction->setMenuRole(QAction::PreferencesRole);
        fileMenu->addSeparator();fileMenu->addAction(settingsAction);
        connect(settingsAction,&QAction::triggered,this,[this]{showSettings();});
        applyBaseFontSize(QSettings().value("appearance/baseFontSize",14).toInt());
        editor->document()->setModified(false);
        render();open->setFocus();
    }

void Window::applyBaseFontSize(int size){
        baseFontSize=qBound(9,size,30);
        const auto range=selection();
        const bool modified=editor->document()->isModified();
        const QSignalBlocker blockEditor(editor);
        QFont ui=defaultUiFont;
        ui.setPointSizeF(qMax(9.0,(ui.pointSizeF()>0?ui.pointSizeF():13)+baseFontSize-13));
        setFont(ui);menuBar()->setFont(ui);
        editor->setFont(QFont(editorFont,baseFontSize+1));
        editor->setTabStopDistance(QFontMetricsF(editor->font()).horizontalAdvance(' ')*4);
        preview->setFont(QFont(previewFont,baseFontSize));
        preview->refreshAppearance();
        static_cast<NumberedEditor<QPlainTextEdit> *>(editor)->refreshLineNumbers();
        preview->refreshLineNumbers();
        selectSource(range.first,range.second);
        editor->document()->setModified(modified);
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

QPair<int,int> Window::selection() const{
        if(previewAction->isChecked())return preview->sourceSelection();
        const auto c=editor->textCursor();return {c.anchor(),c.position()};
    }

void Window::selectSource(int anchor,int position){
        QTextCursor c(editor->document());c.setPosition(anchor);c.setPosition(position,QTextCursor::KeepAnchor);
        editor->setTextCursor(c);
        if(previewAction->isChecked()){render();preview->restoreSourceSelection(anchor,position);}
        else editor->ensureCursorVisible();
    }

void Window::replaceSource(int start,int end,const QString &text,int anchor,int position){
        QTextCursor c(editor->document());c.setPosition(start);c.setPosition(end,QTextCursor::KeepAnchor);
        c.beginEditBlock();c.insertText(text);c.endEditBlock();selectSource(anchor,position);
    }

void Window::formatSelection(const QString &left,const QString &right){
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        if(a>=left.size()&&text.mid(a-left.size(),left.size())==left&&text.mid(b,right.size())==right){
            replaceSource(a-left.size(),b+right.size(),text.mid(a,b-a),a-left.size(),b-left.size());
        }else{
            replaceSource(a,b,left+text.mid(a,b-a)+right,a+left.size(),b+left.size());
        }
    }

void Window::clipboardParagraph(bool cut){
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        if(a==b){a=a?text.lastIndexOf('\n',a-1)+1:0;b=text.indexOf('\n',b);if(b<0)b=text.size();else ++b;}
        QApplication::clipboard()->setText(text.mid(a,b-a));
        if(cut)replaceSource(a,b,{},a,a);
    }

void Window::indentSelection(bool outdent){
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        int start=a?text.lastIndexOf('\n',a-1)+1:0;
        int last=b>a&&text[b-1]=='\n'?b-1:b;
        int end=text.indexOf('\n',last);if(end<0)end=text.size();
        auto lines=text.mid(start,end-start).split('\n');
        int delta=0,firstDelta=0;
        for(int i=0;i<lines.size();++i){
            int d=4;
            if(outdent){int n=0;if(lines[i].startsWith('\t'))n=1;else while(n<4&&n<lines[i].size()&&lines[i][n]==' ')++n;lines[i].remove(0,n);d=-n;}
            else lines[i].prepend("    ");
            if(i==0)firstDelta=d;delta+=d;
        }
        const int na=qMax(start,a+firstDelta),nb=qMax(na,b+delta);
        replaceSource(start,end,lines.join('\n'),range.first<=range.second?na:nb,range.first<=range.second?nb:na);
    }

void Window::showFind(bool replace){
        auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(replace?"查找和替换":"查找");
        auto *layout=new QFormLayout(dialog);auto *query=new QLineEdit;auto *replacement=new QLineEdit;
        const auto r=selection();query->setText(editor->toPlainText().mid(qMin(r.first,r.second),qAbs(r.second-r.first)));
        layout->addRow("查找",query);if(replace)layout->addRow("替换为",replacement);
        auto *sensitive=new QCheckBox("区分大小写");layout->addRow(sensitive);
        auto *message=new QLabel;layout->addRow(message);
        auto find=[this,query,sensitive,message](bool backwards){
            if(query->text().isEmpty())return;
            const auto r=selection();QTextCursor from(editor->document());
            from.setPosition(backwards?qMin(r.first,r.second):qMax(r.first,r.second));
            QTextDocument::FindFlags flags;if(sensitive->isChecked())flags|=QTextDocument::FindCaseSensitively;
            if(backwards)flags|=QTextDocument::FindBackward;
            auto hit=editor->document()->find(query->text(),from,flags);
            if(hit.isNull()){from.movePosition(backwards?QTextCursor::End:QTextCursor::Start);hit=editor->document()->find(query->text(),from,flags);}
            message->setText(hit.isNull()?"未找到匹配项":"");
            if(!hit.isNull())selectSource(hit.selectionStart(),hit.selectionEnd());
        };
        auto *next=new QPushButton("下一个");next->setDefault(true);layout->addRow(next);
        connect(next,&QPushButton::clicked,dialog,[find]{find(false);});
        auto *previous=new QPushButton("上一个");layout->addRow(previous);
        connect(previous,&QPushButton::clicked,dialog,[find]{find(true);});
        if(replace){
            auto *one=new QPushButton("替换");layout->addRow(one);
            connect(one,&QPushButton::clicked,dialog,[this,query,replacement,sensitive,find]{
                const auto r=selection();const int a=qMin(r.first,r.second),b=qMax(r.first,r.second);
                if(a!=b&&!query->text().isEmpty()&&editor->toPlainText().mid(a,b-a).compare(query->text(),sensitive->isChecked()?Qt::CaseSensitive:Qt::CaseInsensitive)==0)
                    replaceSource(a,b,replacement->text(),a,a+replacement->text().size());
                find(false);
            });
            auto *all=new QPushButton("全部替换");layout->addRow(all);
            connect(all,&QPushButton::clicked,dialog,[this,query,replacement,sensitive,message]{
                if(query->text().isEmpty())return;
                const auto before=editor->toPlainText();auto after=before;
                const auto cs=sensitive->isChecked()?Qt::CaseSensitive:Qt::CaseInsensitive;
                int count=0;qsizetype offset=0;
                while((offset=after.indexOf(query->text(),offset,cs))>=0){++count;offset+=query->text().size();}
                after.replace(query->text(),replacement->text(),cs);
                if(after!=before)replaceSource(0,before.size(),after,0,0);
                message->setText(QString("已替换 %1 处").arg(count));
            });
        }
        dialog->open();query->setFocus();query->selectAll();
    }

void Window::updateDocumentStats(){
        const QString text=editor->toPlainText();
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
        const bool modified=editor->document()->isModified();
        if(path.isEmpty()){setWindowTitle("MdEdit");return;}
        const QString name=QFileInfo(path).fileName();
        setWindowTitle(name+(modified?" *":"")+" — MdEdit");
    }

void Window::render(){int scroll=preview->verticalScrollBar()->value();preview->document()->setBaseUrl(QUrl::fromLocalFile((path.isEmpty()?QDir::currentPath():QFileInfo(path).absolutePath())+"/"));QStringList errors;preview->setSource(editor->toPlainText());preview->verticalScrollBar()->setValue(scroll);previewAction->setToolTip(errors.isEmpty()?QString::fromUtf8(shortcutLabel)+"E 切换 Live / 源码":"有公式无法解析，已保留源码："+errors.join("; "));}

bool Window::save(){
        if(path.isEmpty()||!editor->document()->isModified())return true;
        QSaveFile file(path);
        const auto bytes=editor->toPlainText().toUtf8();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
            statusBar()->showMessage("自动保存失败："+file.errorString());
            statusBar()->show();
            return false;
        }
        editor->document()->setModified(false);
        statusBar()->clearMessage();
        return true;
    }

void Window::load(const QString &file){
        if(!save())return;
        QFile f(file);
        if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        const auto bytes=f.readAll();
        if(f.error()!=QFileDevice::NoError){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        path=QFileInfo(file).absoluteFilePath();
        editor->setPlainText(QString::fromUtf8(bytes));
        editor->document()->setModified(false);
        autosave.stop();
        timer.stop();
        render();
        screens->setCurrentWidget(pages);
        bar->show();
        updateDocumentStats();
        statusBar()->show();
        editor->moveCursor(QTextCursor::Start);
        preview->moveCursor(QTextCursor::Start);
        pages->currentWidget()->setFocus();
        editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->minimum());
        preview->verticalScrollBar()->setValue(preview->verticalScrollBar()->minimum());
        updateTitle();
    }

void Window::closeEvent(QCloseEvent *e){
        if(save())e->accept();
        else{
            e->ignore();
            QMessageBox::warning(this,"无法关闭文档",statusBar()->currentMessage());
        }
    }
