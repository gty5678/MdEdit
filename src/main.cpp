#include <QtWidgets>
#include "latex.h"
#include "core/formula.h"
#include "platform/qt/graphic_qt.h"
#include <memory>
#include <functional>

#ifdef Q_OS_MACOS
static const char *editorFont = "Menlo";
static const char *previewFont = "PingFang SC";
static const char *shortcutLabel = "⌘";
#elif defined(Q_OS_WIN)
static const char *editorFont = "Cascadia Code";
static const char *previewFont = "Microsoft YaHei UI";
static const char *shortcutLabel = "Ctrl+";
#else
static const char *editorFont = "monospace";
static const char *previewFont = "sans-serif";
static const char *shortcutLabel = "Ctrl+";
#endif

static QString formulaResourcePath(){
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()+"/../Resources/res";
#else
    return QCoreApplication::applicationDirPath()+"/res";
#endif
}

enum class SpanKind { Code, InlineMath, DisplayMath };
struct Span { int start, length; SpanKind kind; };
static bool escaped(const QString &s,int pos){int n=0;while(pos>0&&s[--pos]=='\\')++n;return n%2;}

// One scanner defines the boundaries for both syntax highlighting and preview.
static QList<Span> scanSpans(const QString &s){
    QList<Span> spans;
    for(int i=0;i<s.size();){
        if(escaped(s,i)){++i;continue;}
        const QChar ch=s[i];
        if(ch=='`'||ch=='~'){
            int count=1;while(i+count<s.size()&&s[i+count]==ch)++count;
            int lineStart=i==0?0:int(s.lastIndexOf('\n',i-1))+1;
            bool fence=count>=3&&i-lineStart<=3&&s.mid(lineStart,i-lineStart).trimmed().isEmpty();
            if(fence){
                int end=int(s.size());int next=int(s.indexOf('\n',i+count));
                while(next>=0&&next+1<s.size()){
                    int start=next+1;next=int(s.indexOf('\n',start));
                    int lineEnd=next<0?int(s.size()):next;
                    QString line=s.mid(start,lineEnd-start);int indent=0;while(indent<line.size()&&line[indent]==' ')++indent;
                    int run=0;while(indent+run<line.size()&&line[indent+run]==ch)++run;
                    if(indent<=3&&run>=count&&line.mid(indent+run).trimmed().isEmpty()){end=lineEnd;break;}
                }
                spans.append({i,end-i,SpanKind::Code});i=end;continue;
            }
            if(ch=='`'){
                int j=i+count,end=-1;
                while(j<s.size()){
                    if(s[j]!='`'){++j;continue;}int run=1;while(j+run<s.size()&&s[j+run]=='`')++run;
                    if(run==count){end=j+run;break;}j+=run;
                }
                if(end>=0){spans.append({i,end-i,SpanKind::Code});i=end;continue;}
            }
            i+=count;continue;
        }
        if(ch=='$'){
            int count=1;while(i+count<s.size()&&s[i+count]=='$')++count;
            if(count>2){i+=count;continue;}
            int j=i+count,end=-1;
            while(j<s.size()){
                if(count==1&&s[j]=='\n')break;
                if(s[j]!='$'||escaped(s,j)){++j;continue;}
                int run=1;while(j+run<s.size()&&s[j+run]=='$')++run;
                if(run==count){end=j+count;break;}j+=run;
            }
            if(end>=0&&!s.mid(i+count,end-i-2*count).trimmed().isEmpty()){
                spans.append({i,end-i,count==2?SpanKind::DisplayMath:SpanKind::InlineMath});i=end;continue;
            }
            i+=count;continue;
        }
        ++i;
    }
    return spans;
}

class Highlighter : public QSyntaxHighlighter {
    QString cachedSource;
    QList<Span> spans;
public:
    explicit Highlighter(QTextDocument *d):QSyntaxHighlighter(d){}
    void highlightBlock(const QString &text) override {
        QString source=document()->toPlainText();if(source!=cachedSource){cachedSource=source;spans=scanSpans(source);}
        const QList<QPair<QString,QString>> rules={{"^#{1,6} .*$","#c3b0fa"},{"\\*\\*.+?\\*\\*","#e6c49c"}};
        for(const auto &r:rules){auto matches=QRegularExpression(r.first).globalMatch(text);while(matches.hasNext()){auto m=matches.next();QTextCharFormat f;f.setForeground(QColor(r.second));setFormat(m.capturedStart(),m.capturedLength(),f);}}
        const int start=currentBlock().position(),end=start+int(text.size());int state=0;
        for(const auto &span:spans){
            if(span.start>end)break;
            int from=qMax(start,span.start),to=qMin(end,span.start+span.length);
            if(to>from){QTextCharFormat f;f.setForeground(QColor(span.kind==SpanKind::Code?"#92c5aa":"#88cbd2"));if(span.kind!=SpanKind::Code)f.setBackground(QColor("#25343c"));setFormat(from-start,to-from,f);}
            if(span.start<=end&&span.start+span.length>end)state=span.kind==SpanKind::Code?1:2;
        }
        // Propagate edits to opening/closing delimiters through following blocks.
        setCurrentBlockState(state);
    }
};

static QString protectMath(const QString &source, QList<QPair<QString,QString>> &math) {
    QString result;int end=0;
    for(const auto &span:scanSpans(source)){
        if(span.kind==SpanKind::Code)continue;
        result+=source.mid(end,span.start-end);
        QString token="MDEDITMATH"+QString::number(math.size())+"TOKEN";while(source.contains(token))token+="X";
        math.append({token,source.mid(span.start,span.length)});
        // A display formula gets its own paragraph even when typed beside text.
        result+=(span.kind==SpanKind::DisplayMath?"\n\n"+token+"\n\n":token);
        end=span.start+span.length;
    }
    return result+source.mid(end);
}

static int renderMarkdown(QTextBrowser *view,const QString &source,QStringList &errors){
    QList<QPair<QString,QString>> math;
    view->setMarkdown(protectMath(source,math));
    int rendered=0;
    for(const auto &entry:math){
        auto cursor=view->document()->find(entry.first);if(cursor.isNull())continue;
        try {
            const bool block=entry.second.startsWith("$$");
            const int delimiter=block?2:1;
            tex::Formula parsed(entry.second.mid(delimiter,entry.second.size()-delimiter*2).toStdWString());
            tex::TeXRenderBuilder builder;
            builder.setStyle(block?tex::TexStyle::display:tex::TexStyle::text)
                .setTextSize(float(QFontMetricsF(view->font()).height()))
                .setForeground(0xffddd9e7).setTrueValues(true);
            // Natural width for inline math; display alignment belongs to its paragraph.
            std::unique_ptr<tex::TeXRender> formula(builder.build(parsed));
            int width=qMax(1,formula->getWidth()+8),height=qMax(1,formula->getHeight()+8);
            if(width>10000||height>10000)throw std::runtime_error("Formula too large");
            QImage image(QSize(width*2,height*2),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);painter.scale(2,2);painter.setRenderHint(QPainter::Antialiasing);tex::Graphics2D_qt graphics(&painter);formula->draw(graphics,4,4);painter.end();
            const QUrl url("math:/"+QUuid::createUuid().toString(QUuid::WithoutBraces));view->document()->addResource(QTextDocument::ImageResource,url,image);
            QTextImageFormat format;format.setName(url.toString());format.setWidth(width);format.setHeight(height);format.setVerticalAlignment(QTextCharFormat::AlignBaseline);
            if(!block)format.setBaselineOffset(-100.0*(formula->getDepth()+4)/QFontMetricsF(view->font()).height());
            cursor.insertImage(format);if(block){auto f=cursor.blockFormat();f.setAlignment(Qt::AlignHCenter);f.setTopMargin(14);f.setBottomMargin(14);cursor.setBlockFormat(f);}++rendered;
        } catch(const std::exception &e){cursor.insertText(entry.second);errors.append(QString::fromUtf8(e.what()));}
        catch(...){cursor.insertText(entry.second);errors.append("无法解析公式");}
    }
    return rendered;
}

// The source remains authoritative; only the paragraph under the caret is exposed.
class LiveEditor : public QTextBrowser {
    struct Region { int sourceStart, sourceEnd, start, end; };
    QList<Region> regions;
    QString source;
    int active=-1;
    bool rebuilding=false;
    QString oldText;
public:
    std::function<void(const QString &)> sourceChanged;
    std::function<void(bool)> history;
    explicit LiveEditor(){
        setReadOnly(false);setAcceptRichText(false);setUndoRedoEnabled(false);setAcceptDrops(false);setContextMenuPolicy(Qt::NoContextMenu);
        connect(this,&QTextEdit::cursorPositionChanged,this,[this]{
            if(rebuilding||textCursor().hasSelection())return;
            int pos=textCursor().position();
            for(int i=0;i<regions.size();++i)if(pos>=regions[i].start&&pos<=regions[i].end){
                if(i!=active)rebuild(i,qMin(pos-regions[i].start,regions[i].sourceEnd-regions[i].sourceStart));
                break;
            }
        });
        connect(this,&QTextEdit::textChanged,this,[this]{
            if(rebuilding||active<0)return;
            QString now=toPlainText();int a=0,b=oldText.size(),c=now.size();
            while(a<b&&a<c&&oldText[a]==now[a])++a;
            while(b>a&&c>a&&oldText[b-1]==now[c-1]){--b;--c;}
            auto &r=regions[active];
            source.replace(r.sourceStart+a-r.start,b-a,now.mid(a,c-a));
            int delta=c-b;r.sourceEnd+=delta;r.end+=delta;
            for(int i=active+1;i<regions.size();++i){regions[i].sourceStart+=delta;regions[i].sourceEnd+=delta;regions[i].start+=delta;regions[i].end+=delta;}
            oldText=now;if(sourceChanged)sourceChanged(source);
        });
    }
    void setSource(const QString &text){if(text==source&&!regions.isEmpty())return;source=text;rebuild(-1,0);}
    QString markdown()const{return source;}
    void rebuild(int chosen,int offset){
        rebuilding=true;int scroll=verticalScrollBar()->value();
        // Blank lines delimit editing units, except within fenced code and formulas.
        auto spans=scanSpans(source);QList<QPair<int,int>> units;int begin=0;
        auto matches=QRegularExpression("\\n[ \\t]*\\n").globalMatch(source);
        while(matches.hasNext()){
            auto m=matches.next();int cut=m.capturedEnd();bool protectedSpan=false;
            for(const auto &span:spans)if(m.capturedStart()>=span.start&&m.capturedStart()<span.start+span.length){protectedSpan=true;break;}
            if(!protectedSpan){units.append({begin,cut});begin=cut;}
        }
        units.append({begin,int(source.size())});
        // Recover chosen unit by source position after edits changed the boundaries.
        int target=chosen>=0&&chosen<regions.size()?regions[chosen].sourceStart+offset:-1;
        active=-1;regions.clear();document()->clear();QTextCursor out(document());
        for(const auto &unit:units){
            int i=regions.size();bool editing=target>=unit.first&&(target<unit.second||unit.second==source.size())&&active<0;
            if(i)out.insertBlock();int from=out.position();
            QString text=source.mid(unit.first,unit.second-unit.first);
            if(editing){active=i;QTextCharFormat f;f.setFont(QFont(editorFont,13));out.setBlockFormat(QTextBlockFormat());out.insertText(text,f);}
            else{
                QTextBrowser rendered;rendered.setFont(font());rendered.document()->setBaseUrl(document()->baseUrl());QStringList errors;
                renderMarkdown(&rendered,text,errors);
                for(auto block=rendered.document()->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
                    auto format=it.fragment().charFormat();if(format.isImageFormat()){
                        QUrl url(format.toImageFormat().name());document()->addResource(QTextDocument::ImageResource,url,rendered.document()->resource(QTextDocument::ImageResource,url));
                    }
                }
                out.insertFragment(QTextDocumentFragment(rendered.document()));
            }
            regions.append({unit.first,unit.second,from,out.position()});
        }
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));setTextCursor(caret);}
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);rebuilding=false;
    }
protected:
    void focusInEvent(QFocusEvent *event)override{
        QTextBrowser::focusInEvent(event);
        if(active<0&&!regions.isEmpty())rebuild(0,0);
    }
    void focusOutEvent(QFocusEvent *event)override{
        QTextBrowser::focusOutEvent(event);
        if(active>=0)rebuild(-1,0);
    }
    void keyPressEvent(QKeyEvent *event)override{
        if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){if(history)history(event->matches(QKeySequence::Redo));return;}
        if(active<0){int pos=textCursor().position();for(int i=0;i<regions.size();++i)if(pos>=regions[i].start&&pos<=regions[i].end){rebuild(i,0);break;}}
        if(active<0){event->ignore();return;}
        if(!textCursor().hasSelection()){
            int pos=textCursor().position();
            if(event->key()==Qt::Key_Backspace&&pos==regions[active].start&&active>0){int previous=active-1;rebuild(previous,regions[previous].sourceEnd-regions[previous].sourceStart);return;}
            if(event->key()==Qt::Key_Delete&&pos==regions[active].end&&active+1<regions.size()){rebuild(active+1,0);return;}
        }
        // A selection spanning rendered regions is expanded to source before editing.
        if(textCursor().hasSelection()&&(textCursor().selectionStart()<regions[active].start||textCursor().selectionEnd()>regions[active].end)){
            auto sourcePosition=[this](int pos){
                for(int i=0;i<regions.size();++i){const auto &r=regions[i];if(pos>=r.start&&pos<=r.end){
                    if(i==active)return r.sourceStart+pos-r.start;
                    if(pos==r.end)return r.sourceEnd;
                    QString visible=toPlainText().mid(r.start,pos-r.start);int at=r.sourceStart;
                    for(QChar ch:visible){
                        if(ch==QChar::ObjectReplacementCharacter){for(const auto &span:scanSpans(source))if(span.start>=at&&span.kind!=SpanKind::Code){at=span.start+span.length;break;}continue;}
                        int found=source.indexOf(ch,at);if(found>=0&&found<r.sourceEnd)at=found+1;
                    }
                    return at;
                }}return int(source.size());
            };
            int begin=sourcePosition(textCursor().selectionStart()),end=sourcePosition(textCursor().selectionEnd());
            rebuilding=true;setPlainText(source);regions={{0,int(source.size()),0,int(source.size())}};active=0;oldText=source;
            QTextCursor selected(document());selected.setPosition(begin);selected.setPosition(end,QTextCursor::KeepAnchor);setTextCursor(selected);rebuilding=false;
        }
        QTextBrowser::keyPressEvent(event);
    }
    void insertFromMimeData(const QMimeData *mime)override{insertPlainText(mime->text());}
};

class Window : public QMainWindow {
    QPlainTextEdit *editor=new QPlainTextEdit;
    LiveEditor *preview=new LiveEditor;
    QStackedWidget *pages=new QStackedWidget;
    QAction *previewAction;
    QTimer timer;
    QString path;
public:
    Window(){
        resize(980,780);setWindowTitle("未命名.md — MdEdit");
        auto *bar=addToolBar("文档");bar->setMovable(false);bar->setContextMenuPolicy(Qt::PreventContextMenu);
        auto *open=bar->addAction("打开");open->setShortcut(QKeySequence::Open);
        auto *saveAction=bar->addAction("保存");saveAction->setShortcut(QKeySequence::Save);
        auto *saveAs= new QAction("另存为",this);saveAs->setShortcut(QKeySequence::SaveAs);addAction(saveAs);
        auto *spacer=new QWidget;spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);bar->addWidget(spacer);
        previewAction=bar->addAction("Live 编辑");previewAction->setCheckable(true);previewAction->setChecked(true);previewAction->setShortcut(QKeySequence("Ctrl+E"));
        previewAction->setObjectName("livePreviewAction");editor->setObjectName("markdownEditor");preview->setObjectName("markdownPreview");
        editor->setFrameShape(QFrame::NoFrame);editor->setFont(QFont(editorFont,13));editor->setTabStopDistance(32);editor->document()->setDocumentMargin(32);new Highlighter(editor->document());
        preview->setFrameShape(QFrame::NoFrame);preview->setFont(QFont(previewFont,12));preview->document()->setDocumentMargin(40);preview->setOpenLinks(false);
        pages->addWidget(editor);pages->addWidget(preview);pages->setCurrentIndex(1);setCentralWidget(pages);
        setStyleSheet(R"(QMainWindow,QWidget{background:#202027;color:#d5d1df;}QToolBar{background:#282830;border:0;border-bottom:1px solid #37343f;padding:8px;spacing:8px;}QToolButton{padding:6px 16px;border:0;border-radius:5px;}QToolButton:hover,QToolButton:checked{background:#493d60;color:#e4d7ff;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#514368;}QScrollBar:vertical{background:#202027;width:10px;}QScrollBar::handle:vertical{background:#4c4658;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#eee;background:#37313f;})");
        timer.setSingleShot(true);timer.setInterval(150);
        preview->sourceChanged=[this](const QString &text){QTextCursor c(editor->document());c.select(QTextCursor::Document);c.insertText(text);};
        preview->history=[this](bool redo){if(redo)editor->redo();else editor->undo();render();};
        connect(editor,&QPlainTextEdit::textChanged,this,[this]{updateTitle();timer.start();});
        connect(&timer,&QTimer::timeout,this,[this]{if(previewAction->isChecked())render();});
        connect(open,&QAction::triggered,this,[this]{auto file=QFileDialog::getOpenFileName(this,"打开 Markdown 文件",path,"Markdown (*.md *.markdown *.txt);;所有文件 (*)");if(!file.isEmpty())load(file);});
        connect(saveAction,&QAction::triggered,this,[this]{save();});connect(saveAs,&QAction::triggered,this,[this]{save(true);});
        connect(previewAction,&QAction::toggled,this,[this](bool enabled){if(enabled)render();pages->setCurrentIndex(enabled?1:0);previewAction->setText(enabled?"Live 编辑":"源码");if(enabled)preview->setFocus();else editor->setFocus();});
        connect(preview,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){if(!url.fragment().isEmpty()&&url.path().isEmpty())preview->scrollToAnchor(url.fragment());else if(url.scheme()=="https"||url.scheme()=="http")QDesktopServices::openUrl(url);});
        editor->setPlaceholderText(QString("打开一个 Markdown 文件，或直接开始写作…\n\n直接在文档中编辑，离开段落后显示排版和公式\n行内公式：$E = mc^2$\n独立公式：$$\\frac{a}{b}$$\n\n%1O 打开 · %1S 保存 · %1E 切换 Live / 源码").arg(QString::fromUtf8(shortcutLabel)));
        render();preview->setFocus();
    }
    void updateTitle(){setWindowTitle((path.isEmpty()?"未命名.md":QFileInfo(path).fileName())+(editor->document()->isModified()?" *":"")+" — MdEdit");}
    void render(){int scroll=preview->verticalScrollBar()->value();preview->document()->setBaseUrl(QUrl::fromLocalFile((path.isEmpty()?QDir::currentPath():QFileInfo(path).absolutePath())+"/"));QStringList errors;preview->setSource(editor->toPlainText());preview->verticalScrollBar()->setValue(scroll);previewAction->setToolTip(errors.isEmpty()?QString::fromUtf8(shortcutLabel)+"E 切换 Live / 源码":"有公式无法解析，已保留源码："+errors.join("; "));}
    bool save(bool as=false){QString target=path;if(as||target.isEmpty())target=QFileDialog::getSaveFileName(this,"保存 Markdown 文件",path.isEmpty()?"未命名.md":path,"Markdown (*.md);;所有文件 (*)");if(target.isEmpty())return false;QSaveFile file(target);if(!file.open(QIODevice::WriteOnly)){QMessageBox::warning(this,"保存失败",file.errorString());return false;}auto bytes=editor->toPlainText().toUtf8();if(file.write(bytes)!=bytes.size()||!file.commit()){QMessageBox::warning(this,"保存失败",file.errorString());return false;}path=target;editor->document()->setModified(false);updateTitle();return true;}
    bool mayLeave(){if(!editor->document()->isModified())return true;auto answer=QMessageBox::question(this,"保存更改","是否保存当前文件的更改？",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);return answer==QMessageBox::Discard||(answer==QMessageBox::Save&&save());}
    void load(const QString &file){if(!mayLeave())return;QFile f(file);if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"打开失败",f.errorString());return;}path=QFileInfo(file).absoluteFilePath();editor->setPlainText(QString::fromUtf8(f.readAll()));editor->document()->setModified(false);if(previewAction->isChecked())render();updateTitle();}
protected:
    void closeEvent(QCloseEvent *e)override{if(mayLeave())e->accept();else e->ignore();}
};

int main(int argc,char **argv){
    QApplication app(argc,argv);app.setStyle("Fusion");
    try{tex::LaTeX::init(formulaResourcePath().toStdString());}catch(const std::exception &e){QMessageBox::critical(nullptr,"公式引擎初始化失败",QString::fromUtf8(e.what()));return 3;}
    int result=0;
    if(app.arguments().contains("--smoke-test")){
        QTextBrowser view;view.resize(900,700);view.setFont(QFont(previewFont,12));QStringList errors;
        int n=renderMarkdown(&view,QString::fromUtf8(R"(# Test
Inline $E=mc^2$ and $\frac{a}{b}$.

$$\int_0^1 x^2\,dx = \frac{1}{3}$$

$$\begin{pmatrix}a & b \\ c & d\end{pmatrix}$$

`$literal$`

```tex
$notmath$
```
)"),errors);
        bool images=n==4&&errors.isEmpty()&&view.toPlainText().contains("$literal$")&&view.toPlainText().contains("$notmath$");
        // Regressions: delimiters, multi-line highlighting, and paragraph isolation.
        const QString sample=QString::fromUtf8(R"(前 $x^2$ 后
$$
\frac{1}{2}
$$
普通正文
`$code$` and \$literal\$
```tex
$$not math$$
```
$$$$
)");
        const auto spans=scanSpans(sample);int inlineCount=0,displayCount=0;
        for(const auto &span:spans){if(span.kind==SpanKind::InlineMath)++inlineCount;if(span.kind==SpanKind::DisplayMath)++displayCount;}
        images=images&&inlineCount==1&&displayCount==1;
        images=images&&scanSpans("```tex\n$x$\n```").size()==1;
        images=images&&scanSpans("```tex\n$x$").size()==1;
        QTextDocument doc;doc.setPlainText(sample);Highlighter highlighter(&doc);highlighter.rehighlight();
        auto colorAt=[&](int block,int offset){for(const auto &range:doc.findBlockByNumber(block).layout()->formats())if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();return QColor();};
        images=images&&colorAt(1,0)==QColor("#88cbd2")&&colorAt(2,0)==QColor("#88cbd2")&&colorAt(3,0)==QColor("#88cbd2");
        images=images&&!colorAt(4,0).isValid()&&colorAt(7,0)==QColor("#92c5aa");
        QTextBrowser isolation;isolation.setFont(view.font());isolation.resize(900,700);QStringList isolationErrors;
        images=images&&renderMarkdown(&isolation,QString::fromUtf8("前文 $$\\frac{a}{b}$$ 后文 $x^2$ 结束"),isolationErrors)==2&&isolationErrors.isEmpty();
        bool centeredImage=false,inlineImage=false;
        for(auto block=isolation.document()->begin();block.isValid();block=block.next()){
            if(block.text().contains(QChar::ObjectReplacementCharacter)){
                if(block.blockFormat().alignment().testFlag(Qt::AlignHCenter))centeredImage=block.text().trimmed()==QString(QChar::ObjectReplacementCharacter);
                else if(block.text().contains(QString::fromUtf8("后文"))){for(auto i=block.begin();!i.atEnd();++i)if(i.fragment().charFormat().isImageFormat())inlineImage=i.fragment().charFormat().toImageFormat().width()<100;}
            }
            if(block.text().contains(QString::fromUtf8("前文")))images=images&&!block.blockFormat().alignment().testFlag(Qt::AlignHCenter);
        }
        images=images&&centeredImage&&inlineImage;
        // Verify edits really reach the preview through the debounce timer.
        auto checkLive=[&](bool ok,int line){if(!ok){QFile log(QDir::current().absoluteFilePath("live-failures.txt"));log.open(QIODevice::Append);log.write(QByteArray::number(line)+"\n");}return ok;};
        Window live;
        auto *edit=live.findChild<QPlainTextEdit *>("markdownEditor");
        auto *output=live.findChild<QTextBrowser *>("markdownPreview");
        auto *toggle=live.findChild<QAction *>("livePreviewAction");
        auto settle=[] {QEventLoop loop;QTimer::singleShot(250,&loop,&QEventLoop::quit);loop.exec();};
        images=checkLive(toggle->isChecked()&&edit->isHidden()&&!output->isHidden(),__LINE__)&&images;
        edit->setPlainText("# Live heading\n\nFirst $x^2$");settle();
        images=checkLive(output->toPlainText().contains("Live heading")&&!output->toPlainText().contains("$x^2$"),__LINE__)&&images;
        edit->selectAll();edit->insertPlainText("Updated **content**");settle();
        images=checkLive(output->toPlainText().contains("Updated content")&&!output->toPlainText().contains("Live heading"),__LINE__)&&images;
        edit->undo();settle();images=checkLive(output->toPlainText().contains("Live heading"),__LINE__)&&images;
        toggle->setChecked(false);edit->setPlainText("Hidden edit");settle();
        images=checkLive(output->isHidden()&&!edit->isHidden(),__LINE__)&&images;
        toggle->setChecked(true);images=checkLive(!output->isHidden()&&output->toPlainText().contains("Hidden edit"),__LINE__)&&images;
        edit->clear();settle();images=checkLive(output->toPlainText().isEmpty(),__LINE__)&&images;
        auto *liveOutput=static_cast<LiveEditor *>(output);
        edit->setPlainText("# Heading\n\nBody $x^2$");settle();
        auto caret=output->textCursor();caret.setPosition(2);output->setTextCursor(caret);
        images=checkLive(output->toPlainText().startsWith("# Heading"),__LINE__)&&images;
        output->insertPlainText("X");settle();
        images=checkLive(edit->toPlainText().startsWith("# XHeading")&&liveOutput->markdown()==edit->toPlainText(),__LINE__)&&images;
        caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
        images=checkLive(!output->toPlainText().startsWith("#")&&output->toPlainText().contains("$x^2$"),__LINE__)&&images;
        edit->document()->setModified(false);
        QImage shot(view.size(),QImage::Format_ARGB32);shot.fill(QColor("#202027"));view.setStyleSheet("color:#ddd9e7;background:#202027;");view.render(&shot);shot.save(QDir::current().absoluteFilePath("math-smoke.png"));
        if(!images){qWarning()<<n<<errors;result=1;}
    }else{Window w;if(app.arguments().size()>1)w.load(app.arguments().at(1));w.show();result=app.exec();}
    tex::LaTeX::release();return result;
}




