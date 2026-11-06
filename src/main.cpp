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

// Keep line numbers outside the document so they never enter copied/saved text.
class LineNumberArea : public QWidget {
public:
    std::function<void(QPaintEvent *)> paint;
    explicit LineNumberArea(QWidget *parent):QWidget(parent){setObjectName("lineNumberArea");setAttribute(Qt::WA_TransparentForMouseEvents);}
protected:
    void paintEvent(QPaintEvent *event)override{if(paint)paint(event);}
};

template<class Base> class NumberedEditor : public Base {
    LineNumberArea *numbers=new LineNumberArea(this);
    int gutterWidth=0;
public:
    NumberedEditor(){
        numbers->paint=[this](QPaintEvent *event){paintNumbers(event);};
        QObject::connect(this,&Base::textChanged,this,[this]{refreshLineNumbers();});
        QObject::connect(this->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{numbers->update();});
        QObject::connect(this,&Base::cursorPositionChanged,this,[this]{numbers->update();});
        QObject::connect(this->document()->documentLayout(),&QAbstractTextDocumentLayout::update,this,[this]{numbers->update();});
        refreshLineNumbers();
    }
    int lineNumberForBlock(const QTextBlock &block)const{return lineNumberAt(QTextCursor(block));}
    virtual int lineNumberAt(const QTextCursor &cursor)const{return cursor.blockNumber()+1;}
    virtual int sourceLineCount()const{return this->document()->blockCount();}
    void refreshLineNumbers(){
        QFontMetrics metrics(QFont(editorFont,10));
        int width=24+metrics.horizontalAdvance(QString::number(qMax(1,sourceLineCount())));
        if(width!=gutterWidth){gutterWidth=width;this->setViewportMargins(width,0,0,0);}
        const auto area=this->contentsRect();numbers->setGeometry(area.left(),area.top(),width,area.height());
        numbers->update();
    }
protected:
    void resizeEvent(QResizeEvent *event)override{Base::resizeEvent(event);refreshLineNumbers();}
private:
    void paintNumbers(QPaintEvent *event){
        QPainter painter(numbers);painter.fillRect(event->rect(),QColor("#202027"));
        painter.setFont(QFont(editorFont,10));
        const int current=lineNumberAt(this->textCursor());
        auto block=this->cursorForPosition(QPoint(0,0)).block();int previous=-1;
        while(block.isValid()){
            QTextCursor cursor(block);const auto rect=this->cursorRect(cursor);
            if(rect.top()>this->viewport()->height())break;
            const QString text=block.text();
            for(int start=0;;){
                cursor.setPosition(block.position()+start);
                const auto lineRect=this->cursorRect(cursor);
                if(lineRect.top()>this->viewport()->height())break;
                const int number=lineNumberAt(cursor);
                if(lineRect.bottom()>=0&&number>0&&number!=previous){
                    painter.setPen(number==current?QColor("#c3b0fa"):QColor("#777180"));
                    painter.drawText(QRect(0,lineRect.top(),gutterWidth-10,qMax(lineRect.height(),painter.fontMetrics().height())),
                        Qt::AlignRight|Qt::AlignVCenter,QString::number(number));
                    previous=number;
                }
                const int separator=text.indexOf(QChar::LineSeparator,start);
                if(separator<0)break;
                start=separator+1;
            }
            block=block.next();
        }
    }
};

// The source remains authoritative; inline editing exposes just the active span.
class LiveEditor : public NumberedEditor<QTextBrowser> {
    struct Region { int sourceStart, sourceEnd, start, end, contentEnd=-1; bool inlinePart=false; };
    QList<Region> regions;
    QString source;
    int active=-1;
    bool rebuilding=false;
    bool movingCursor=false;
    QString oldText;
public:
    std::function<void(const QString &)> sourceChanged;
    std::function<void(bool)> history;
    explicit LiveEditor(){
        setReadOnly(false);setAcceptRichText(false);setUndoRedoEnabled(false);setAcceptDrops(false);setContextMenuPolicy(Qt::NoContextMenu);
        connect(this,&QTextEdit::cursorPositionChanged,this,[this]{
            // Cursor signals can arrive before textChanged during a multiline edit.
            // Wait for source/region offsets to catch up before switching units.
            if(rebuilding||movingCursor||textCursor().hasSelection()||toPlainText()!=oldText)return;
            activateCaretRegion();
        });
        connect(this,&QTextEdit::textChanged,this,[this]{
            if(rebuilding||active<0)return;
            const QString now=toPlainText();
            auto &r=regions[active];
            const int delta=now.size()-oldText.size();
            // Equal separators outside the editable region can otherwise
            // absorb a newly inserted newline during the prefix scan.
            int a=r.start,b=r.end,c=r.end+delta;
            while(a<b&&a<c&&oldText[a]==now[a])++a;
            while(b>a&&c>a&&oldText[b-1]==now[c-1]){--b;--c;}
            source.replace(r.sourceStart+a-r.start,b-a,now.mid(a,c-a));
            r.sourceEnd+=delta;r.end+=delta;if(r.contentEnd>=0)r.contentEnd+=delta;
            for(int i=active+1;i<regions.size();++i){regions[i].sourceStart+=delta;regions[i].sourceEnd+=delta;if(regions[i].contentEnd>=0)regions[i].contentEnd+=delta;regions[i].start+=delta;regions[i].end+=delta;}
            oldText=now;styleActiveSource();refreshLineNumbers();if(sourceChanged)sourceChanged(source);
        });
    }
    void setSource(const QString &text){if(text==source&&!regions.isEmpty())return;source=text;rebuild(-1,0);}
    QString markdown()const{return source;}
    int sourceLineCount()const override{return source.count('\n')+1;}
    int lineNumberAt(const QTextCursor &cursor)const override{
        return source.left(sourcePosition(cursor.position())).count('\n')+1;
    }
    void rebuild(int chosen,int offset,int forcedTarget=-1){
        rebuilding=true;int scroll=verticalScrollBar()->value();
        const int caretY=cursorRect().top();
        // Give each source blank line its own editable block. Keep fenced
        // code and display formulas intact for their respective renderers.
        const auto spans=scanSpans(source);
        QList<QPair<int,int>> units;
        int begin=0;
        static const QRegularExpression blankLine("(?:^|(?<=\\n))[ \\t]*\\n");
        auto matches=blankLine.globalMatch(source);
        while(matches.hasNext()){
            const auto match=matches.next();
            const int start=match.capturedStart();
            bool protectedSpan=false;
            for(const auto &span:spans){
                if(span.start>start)break;
                if(start<span.start+span.length){protectedSpan=true;break;}
            }
            if(protectedSpan)continue;
            if(begin<start)units.append({begin,start});
            units.append({start,match.capturedEnd()});
            begin=match.capturedEnd();
        }
        if(begin<source.size())units.append({begin,int(source.size())});
        if(source.isEmpty()||source.endsWith('\n'))units.append({int(source.size()),int(source.size())});
        // Recover chosen unit by source position after edits changed the boundaries.
        int target=forcedTarget>=0?forcedTarget:(chosen>=0&&chosen<regions.size()?regions[chosen].sourceStart+offset:-1);
        active=-1;regions.clear();document()->clear();QTextCursor out(document());
        for(qsizetype unitIndex=0;unitIndex<units.size();++unitIndex){
            const auto &unit=units[unitIndex];
            int i=regions.size();bool editing=target>=unit.first&&(target<unit.second||unitIndex==units.size()-1)&&active<0;
            if(i)out.insertBlock(QTextBlockFormat(),QTextCharFormat());int from=out.position();
            QString text=source.mid(unit.first,unit.second-unit.first);
            // The boundary between units already inserts one newline.
            if(text.endsWith('\n'))text.chop(1);
            const int contentEnd=unit.first+text.size();
            const auto pieces=inlinePieces(text);
            if(!pieces.isEmpty()){
                QTextDocument body;body.setMarkdown("Body");
                out.setBlockFormat(body.begin().blockFormat());
                QTextCharFormat normal;normal.setFont(font());
                int begin=0;
                for(const auto &piece:pieces){
                    const int end=piece.first;
                    const int start=out.position();
                    const bool selected=editing&&target>=unit.first+begin&&
                        (target<unit.first+end||end==text.size());
                    const QString raw=text.mid(begin,end-begin);
                    if(selected||!piece.second){
                        if(selected)active=regions.size();
                        QString displayed=raw;
                        displayed.replace('\n',QChar::LineSeparator);
                        out.insertText(displayed,normal);
                    }else{
                        QTextBrowser rendered;rendered.setFont(font());QStringList errors;
                        renderMarkdown(&rendered,raw,errors);
                        for(auto it=rendered.document()->begin().begin();!it.atEnd();++it){
                            const auto fragment=it.fragment();
                            auto format=fragment.charFormat();
                            if(format.isImageFormat()){
                                const QUrl url(format.toImageFormat().name());
                                document()->addResource(QTextDocument::ImageResource,url,
                                    rendered.document()->resource(QTextDocument::ImageResource,url));
                            }
                            out.insertText(fragment.text(),format);
                        }
                    }
                    regions.append({unit.first+begin,end==text.size()?unit.second:unit.first+end,
                        start,out.position(),unit.first+end,true});
                    begin=end;
                }
                continue;
            }
            if(editing){
                active=i;QTextCharFormat f;f.setFont(font());out.setBlockFormat(QTextBlockFormat());out.insertText(text,f);
            }else if(text.trimmed().isEmpty()){
                QTextCharFormat f;f.setFont(font());out.setBlockCharFormat(f);
            }else{
                QTextBrowser rendered;rendered.setFont(font());rendered.document()->setBaseUrl(document()->baseUrl());QStringList errors;
                // Keep soft source breaks visible in ordinary paragraphs so
                // entering edit mode doesn't unfold them into extra rows.
                QString layoutText=text;
                const bool joinedParagraph=isMultilineParagraph(text);
                if(joinedParagraph)layoutText.replace("\n","  \n");
                renderMarkdown(&rendered,layoutText,errors);
                for(auto block=rendered.document()->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
                    auto format=it.fragment().charFormat();if(format.isImageFormat()){
                        QUrl url(format.toImageFormat().name());document()->addResource(QTextDocument::ImageResource,url,rendered.document()->resource(QTextDocument::ImageResource,url));
                    }
                }
                if(joinedParagraph){
                    for(auto block=rendered.document()->begin();block.isValid();block=block.next()){
                        auto format=block.blockFormat();
                        if(block.previous().isValid())format.setTopMargin(0);
                        if(block.next().isValid())format.setBottomMargin(0);
                        QTextCursor(block).setBlockFormat(format);
                    }
                }
                out.insertFragment(QTextDocumentFragment(rendered.document()));
                // Qt can keep the destination block format for the first
                // paragraph of a fragment inserted into an empty block.
                QTextCursor first(document()->findBlock(from));
                const auto format=rendered.document()->begin().blockFormat();
                if(format.objectIndex()<0&&first.blockFormat().objectIndex()<0)
                    first.setBlockFormat(format);
            }
            regions.append({unit.first,unit.second,from,out.position(),contentEnd});
        }
        styleActiveSource();
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));setTextCursor(caret);}
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);
        if(target>=0)verticalScrollBar()->setValue(scroll+cursorRect().top()-caretY);
        rebuilding=false;refreshLineNumbers();
    }
protected:
    void mousePressEvent(QMouseEvent *event)override{
        int formulaRegion=-1;
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            const auto point=event->position().toPoint();
            const int hit=cursorForPosition(point).position();
            for(int pos:{hit,hit-1}){
                if(pos<0||pos>=oldText.size()||oldText[pos]!=QChar::ObjectReplacementCharacter)continue;
                const int region=regionAt(pos);
                if(region<0||!regions[region].inlinePart)continue;
                QTextCursor edge(document());edge.setPosition(pos);
                const auto left=cursorRect(edge);
                edge.setPosition(pos+1);
                const auto right=cursorRect(edge);
                if(left.united(right).contains(point)){formulaRegion=region;break;}
            }
        }
        {
            QScopedValueRollback<bool> guard(movingCursor,true);
            QTextBrowser::mousePressEvent(event);
        }
        if(formulaRegion>=0)rebuild(formulaRegion,0);
        else activateCaretRegion();
    }
    void focusInEvent(QFocusEvent *event)override{
        QTextBrowser::focusInEvent(event);
        if(active<0&&!regions.isEmpty())rebuild(0,0);
    }
    void focusOutEvent(QFocusEvent *event)override{
        QTextBrowser::focusOutEvent(event);
        if(active>=0)rebuild(-1,0);
    }
    void keyPressEvent(QKeyEvent *event)override{
        const bool newline=event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter;
        if(newline&&!(event->modifiers()&~Qt::KeypadModifier)){
            const auto cursor=textCursor();
            const bool all=cursor.hasSelection()&&cursor.selectionStart()==0&&
                cursor.selectionEnd()==document()->characterCount()-1;
            const int start=all?0:sourcePosition(cursor.selectionStart());
            const int end=all?int(source.size()):sourcePosition(cursor.selectionEnd());
            source.replace(start,qMax(0,end-start),"\n");
            rebuild(-1,0,start+1);
            if(sourceChanged)sourceChanged(source);
            event->accept();
            return;
        }
        if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){int at=sourcePosition(textCursor().position());
            if(history)history(event->matches(QKeySequence::Redo));
            for(int i=0;i<regions.size();++i)if(at>=regions[i].sourceStart&&(at<=regions[i].sourceEnd)){
                rebuild(i,at-regions[i].sourceStart);break;
            }
            return;}
        if(active<0)activateCaretRegion();
        if(active<0){event->ignore();return;}
        if(!textCursor().hasSelection()){
            int pos=textCursor().position();
            if(event->key()==Qt::Key_Backspace&&pos==regions[active].start&&active>0){int at=sourcePosition(pos);exposeSource(at,at);}
            if(event->key()==Qt::Key_Delete&&pos==regions[active].end&&active+1<regions.size()){int at=sourcePosition(pos);exposeSource(at,at);}
        }
        if(newline||event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete||
           event->matches(QKeySequence::Cut)||event->matches(QKeySequence::Paste)||
           (!event->text().isEmpty()&&!(event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier))))prepareSelection();
        {
            // QTextControl still uses the old document positions until its key
            // handler returns. Rebuild only after that movement has finished.
            QScopedValueRollback<bool> guard(movingCursor,true);
            if(newline)QTextEdit::keyPressEvent(event);
            else QTextBrowser::keyPressEvent(event);
        }
        if(toPlainText()==oldText)activateCaretRegion();
    }
    void insertFromMimeData(const QMimeData *mime)override{prepareSelection();insertPlainText(mime->text());}
    void inputMethodEvent(QInputMethodEvent *event)override{
        prepareSelection();
        QTextBrowser::inputMethodEvent(event);
    }
private:
    int regionAt(int pos)const{
        // At an inline boundary the following span owns the caret.
        for(int i=0;i<regions.size();++i){
            const auto &r=regions[i];
            if(pos<r.start||pos>r.end)continue;
            if(pos==r.end&&i+1<regions.size()&&regions[i+1].start==pos)continue;
            return i;
        }
        return -1;
    }
    void activateCaretRegion(){
        const auto caret=textCursor();
        if(caret.hasSelection())return;
        const int i=regionAt(caret.position());
        if(i>=0&&i!=active)rebuild(i,sourcePosition(caret.position())-regions[i].sourceStart);
    }
    static QList<QPair<int,bool>> inlinePieces(const QString &text){
        // Use independent inline spans only where they don't change the
        // surrounding Markdown structure. Complex blocks use the full parser.
        if(text.startsWith(' ')||text.startsWith('\t'))return {};
        if(text.contains('\n')&&!isMultilineParagraph(text))return {};
        QList<QPair<int,int>> tokens;
        for(const auto &span:scanSpans(text)){
            if(span.kind!=SpanKind::InlineMath)return {};
            tokens.append({span.start,span.start+span.length});
        }
        static const QRegularExpression strong(R"((?<![\\*])\*\*([^*\n]+)\*\*(?!\*))");
        auto matches=strong.globalMatch(text);
        while(matches.hasNext()){
            const auto match=matches.next();
            const auto inner=match.captured(1);
            if(inner.front().isSpace()||inner.back().isSpace())return {};
            tokens.append({match.capturedStart(),match.capturedEnd()});
        }
        if(tokens.isEmpty())return {};
        std::sort(tokens.begin(),tokens.end());
        static const QRegularExpression otherMarkup(R"([#>*_`~\[\]<&\\$]|^[-+ ]|^\d+[.)] )");
        QList<QPair<int,bool>> pieces;
        int at=0;
        for(const auto &token:tokens){
            if(token.first<at)return {};
            if(token.first>at){
                if(otherMarkup.match(text.mid(at,token.first-at)).hasMatch()&&
                    (at==0||otherMarkup.match(text.mid(at,token.first-at).trimmed()).hasMatch()))return {};
                pieces.append({token.first,false});
            }
            pieces.append({token.second,true});
            at=token.second;
        }
        if(at<text.size()){
            if(otherMarkup.match(text.mid(at).trimmed()).hasMatch())return {};
            pieces.append({text.size(),false});
        }
        return pieces;
    }
    static bool isMultilineParagraph(const QString &text){
        if(!text.contains('\n'))return false;
        for(const auto &span:scanSpans(text))
            if(span.kind==SpanKind::DisplayMath||text.mid(span.start,span.length).contains('\n'))return false;
        // A terminal backslash is already a Markdown hard break.
        if(text.contains("\\\n"))return false;
        QTextDocument reference;reference.setMarkdown(text);
        const auto block=reference.begin();
        const auto format=block.blockFormat();
        return reference.blockCount()==1&&!block.textList()&&!format.headingLevel()&&
            !format.indent()&&!format.hasProperty(QTextFormat::BlockCodeLanguage)&&
            !format.hasProperty(QTextFormat::BlockQuoteLevel)&&!format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    }
    void styleActiveSource(){
        if(active<0||regions[active].inlinePart)return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const auto &region=regions[active];
        const auto spans=scanSpans(source);
        const bool joinedParagraph=isMultilineParagraph(source.mid(region.sourceStart,region.end-region.start));
        QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
        static const QRegularExpression heading("^ {0,3}(#{1,6})(?:[ \t]+|$)");
        for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next()){
            QTextCharFormat characters;characters.setFont(font());
            QTextBlockFormat paragraph=block.text().trimmed().isEmpty()?QTextBlockFormat():bodyStyle.begin().blockFormat();
            if(joinedParagraph){
                if(block.position()>region.start)paragraph.setTopMargin(0);
                if(block.next().isValid()&&block.next().position()<=region.end)paragraph.setBottomMargin(0);
            }
            const int at=region.sourceStart+block.position()-region.start;
            bool code=false;
            for(const auto &span:spans)if(span.kind==SpanKind::Code&&at>=span.start&&at<span.start+span.length){code=true;break;}
            const auto match=heading.match(block.text());
            if(!code&&match.hasMatch()){
                // Ask Qt for its preview heading style, then apply it to the
                // whole source line, including the visible Markdown markers.
                QTextDocument reference;reference.setDefaultFont(font());
                reference.setMarkdown(match.captured(1)+" Heading");
                auto rendered=reference.begin();
                characters.merge(rendered.begin().fragment().charFormat());
                paragraph=rendered.blockFormat();
            }
            QTextCursor line(block);line.setPosition(block.position()+block.text().size(),QTextCursor::KeepAnchor);
            line.setBlockFormat(paragraph);line.setBlockCharFormat(characters);line.setCharFormat(characters);
        }
    }
    int sourcePosition(int pos)const{
        const int i=regionAt(pos);
        if(i>=0){const auto &r=regions[i];
            if(i==active)return r.sourceStart+pos-r.start;
            if(r.start==r.end)return r.sourceStart;
            if(pos==r.end)return r.contentEnd>=0?r.contentEnd:r.sourceEnd;
            QString visible=oldText.mid(r.start,r.end-r.start);int at=r.sourceStart;
            // Walk one character ahead to skip leading Markdown delimiters too.
            for(int j=0;j<=pos-r.start&&j<visible.size();++j){
                QChar ch=visible[j];
                if(ch==QChar::ObjectReplacementCharacter){
                    for(const auto &span:scanSpans(source))if(span.start>=at&&span.start<r.sourceEnd&&span.kind!=SpanKind::Code){
                        if(j==pos-r.start)return span.start;
                        at=span.start+span.length;break;
                    }
                    continue;
                }
                int found=source.indexOf(ch,at);
                if(found>=0&&found<r.sourceEnd){if(j==pos-r.start)return found;at=found+1;}
            }
            return at;
        }return int(source.size());
    }
    void exposeSource(int begin,int end){
        rebuilding=true;setPlainText(source);
        regions={{0,int(source.size()),0,int(source.size())}};active=0;oldText=source;
        QTextCursor selected(document());selected.setPosition(qBound(0,begin,int(source.size())));
        selected.setPosition(qBound(0,end,int(source.size())),QTextCursor::KeepAnchor);
        setTextCursor(selected);styleActiveSource();rebuilding=false;
    }
    void prepareSelection(){
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1){
            exposeSource(caret.anchor()<caret.position()?0:int(source.size()),
                         caret.anchor()<caret.position()?int(source.size()):0);return;
        }
        if(active<0||caret.selectionStart()<regions[active].start||caret.selectionEnd()>regions[active].end)
            exposeSource(sourcePosition(caret.anchor()),sourcePosition(caret.position()));
    }
};

class Window : public QMainWindow {
    QPlainTextEdit *editor=new NumberedEditor<QPlainTextEdit>;
    LiveEditor *preview=new LiveEditor;
    QStackedWidget *pages=new QStackedWidget;
    QAction *previewAction;
    QTimer timer;
    QString path;
public:
    Window(){
        resize(980,780);setWindowTitle("未命名.md — MdEdit");
        auto *bar=addToolBar("文档");bar->setMovable(false);bar->setContextMenuPolicy(Qt::PreventContextMenu);
        auto *open=bar->addAction("打开");open->setObjectName("openDocumentAction");open->setShortcut(QKeySequence::Open);
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
        preview->sourceChanged=[this](const QString &text){
            const QString before=editor->toPlainText();int begin=0,oldEnd=before.size(),newEnd=text.size();
            while(begin<oldEnd&&begin<newEnd&&before[begin]==text[begin])++begin;
            while(oldEnd>begin&&newEnd>begin&&before[oldEnd-1]==text[newEnd-1]){--oldEnd;--newEnd;}
            if(begin==oldEnd&&begin==newEnd)return;
            QTextCursor c(editor->document());c.setPosition(begin);c.setPosition(oldEnd,QTextCursor::KeepAnchor);
            c.insertText(text.mid(begin,newEnd-begin));
        };
        preview->history=[this](bool redo){if(redo)editor->redo();else editor->undo();render();};
        connect(editor,&QPlainTextEdit::textChanged,this,[this]{updateTitle();timer.start();});
        connect(&timer,&QTimer::timeout,this,[this]{if(previewAction->isChecked())render();});
        connect(open,&QAction::triggered,this,[this]{
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
    #ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
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
        auto checkLive=[&](bool ok,int line){if(!ok){qWarning()<<"Live check failed at"<<line;QFile log(QDir::current().absoluteFilePath("live-failures.txt"));if(log.open(QIODevice::Append))log.write(QByteArray::number(line)+"\n");}return ok;};
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
        images=checkLive(edit->toPlainText().startsWith("# HeXading")&&liveOutput->markdown()==edit->toPlainText(),__LINE__)&&images;
        caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
        images=checkLive(!output->toPlainText().startsWith("#")&&output->toPlainText().contains("$x^2$"),__LINE__)&&images;
        // Clicking visible text must account for hidden Markdown punctuation.
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        images=checkLive(output->textCursor().position()==2,__LINE__)&&images;
        // Keep the same visible blank lines when activating a paragraph.
        for(const QString &first:{QString("First"),QString("# Heading")}){
            const QString original=first+"\n\nSecond\n\nThird";
            edit->setPlainText(original);settle();
            const int blocks=output->document()->blockCount();
            const auto next=output->document()->findBlock(output->toPlainText().indexOf("Second"));
            const qreal beforeY=output->document()->documentLayout()->blockBoundingRect(next).top();
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            const auto after=output->document()->findBlock(output->toPlainText().indexOf("Second"));
            const qreal afterY=output->document()->documentLayout()->blockBoundingRect(after).top();
            images=checkLive(blocks==5&&output->document()->blockCount()==blocks&&qAbs(beforeY-afterY)<1.0&&
                edit->toPlainText()==original,__LINE__)&&images;
            output->insertPlainText("X");
            images=checkLive(edit->toPlainText().contains("\n\nSecond\n\nThird"),__LINE__)&&images;
            caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
            images=checkLive(output->toPlainText().contains("\n\n")&&
                edit->toPlainText().contains("\n\nSecond\n\nThird"),__LINE__)&&images;
        }
        // Enter must use the editor behavior, and existing soft breaks must
        // keep the following paragraph at the same height when activated.
        edit->setPlainText("First\nSecond\n\nThird");settle();
        auto following=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        const qreal restingY=output->document()->documentLayout()->blockBoundingRect(following).top();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));
        images=checkLive(liveOutput->lineNumberAt(caret)==2,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        following=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        images=checkLive(qAbs(restingY-output->document()->documentLayout()->blockBoundingRect(following).top())<1.0,__LINE__)&&images;
        caret=output->textCursor();caret.movePosition(QTextCursor::EndOfBlock);output->setTextCursor(caret);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
        QApplication::sendEvent(output,&enter);
        images=checkLive(edit->toPlainText()=="First\n\nSecond\n\nThird"&&
            liveOutput->lineNumberForBlock(output->textCursor().block())==2,__LINE__)&&images;
        // Repeated Enter keeps the caret on each new blank line and inserts
        // before any existing whitespace-only lines, even when hidden.
        for(const QString &tail:{QString(),QString("\n \t\n\n# Next")}){
            edit->setPlainText("First"+tail);settle();
            caret=output->textCursor();caret.setPosition(5);output->setTextCursor(caret);
            for(int count=1;count<=3;++count){
                QKeyEvent nextEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
                QApplication::sendEvent(output,&nextEnter);
                const QString expected="First"+QString(count,'\n')+tail;
                images=checkLive(edit->toPlainText()==expected&&liveOutput->markdown()==expected&&
                    output->textCursor().block().text().isEmpty()&&
                    liveOutput->lineNumberAt(output->textCursor())==count+1,__LINE__)&&images;
            }
            output->insertPlainText("New");
            images=checkLive(edit->toPlainText()=="First\n\n\nNew"+tail,__LINE__)&&images;
        }
        // Leading, consecutive, whitespace-only and trailing blank lines survive.
        const QString spaced="\nFirst\n\n \t\n\nSecond\n";
        edit->setPlainText(spaced);settle();
        images=checkLive(output->toPlainText()=="\nFirst\n\n\n\nSecond\n"&&
            output->document()->blockCount()==7&&edit->toPlainText()==spaced,__LINE__)&&images;
        const auto blank=output->document()->findBlockByNumber(2);
        images=checkLive(liveOutput->lineNumberForBlock(blank)==3,__LINE__)&&images;
        output->setTextCursor(QTextCursor(blank));output->insertPlainText("New text");
        images=checkLive(edit->toPlainText()=="\nFirst\nNew text\n \t\n\nSecond\n",__LINE__)&&images;
        // A down arrow landing at a paragraph's end must not skip that paragraph.
        edit->setPlainText("aa\n\nbb\n\ncc");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        caret=output->textCursor();caret.movePosition(QTextCursor::EndOfBlock);output->setTextCursor(caret);
        QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
        for(const QString &expected:{QString(),QString("bb"),QString(),QString("cc")}){
            QApplication::sendEvent(output,&down);
            images=checkLive(output->textCursor().block().text()==expected,__LINE__)&&images;
        }
        QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
        QApplication::sendEvent(output,&up);
        images=checkLive(output->textCursor().block().text().isEmpty(),__LINE__)&&images;
        images=checkLive(edit->toPlainText()=="aa\n\nbb\n\ncc",__LINE__)&&images;
        edit->setPlainText("first\n\nsecond\n\nthird");settle();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("second")+6);output->setTextCursor(caret);
        images=checkLive(output->textCursor().block().text()=="second"&&output->textCursor().positionInBlock()==6,__LINE__)&&images;
        // Live gutters count blank lines as source lines too.
        edit->setPlainText("# Heading\n\nSecond\n\nThird");settle();
        for(const auto &entry:QList<QPair<QString,int>>{{"Heading",1},{"Second",3},{"Third",5}}){
            auto block=output->document()->findBlock(output->toPlainText().indexOf(entry.first));
            images=checkLive(liveOutput->lineNumberForBlock(block)==entry.second,__LINE__)&&images;
        }
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));output->setTextCursor(caret);
        output->insertPlainText("New line\n");
        auto third=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        images=checkLive(liveOutput->lineNumberForBlock(third)==6&&liveOutput->sourceLineCount()==6&&
            edit->toPlainText()=="# Heading\n\nNew line\nSecond\n\nThird",__LINE__)&&images;
        images=checkLive(edit->findChild<QWidget *>("lineNumberArea")&&
            output->findChild<QWidget *>("lineNumberArea"),__LINE__)&&images;
        // Editing headings keeps the preview size, including every '#' marker.
        for(int level=1;level<=6;++level){
            const QString heading=QString(level,'#')+" Heading";
            edit->setPlainText(heading+"\n\nBody");settle();
            QTextDocument reference;reference.setDefaultFont(output->font());reference.setMarkdown(heading);
            const auto expected=reference.begin().begin().fragment().charFormat();
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            for(int pos:{0,level+1}){
                QTextCursor sample(output->document());sample.setPosition(pos);sample.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                auto actual=sample.charFormat();
                images=checkLive(actual.fontWeight()==expected.fontWeight()&&
                    actual.property(QTextFormat::FontSizeAdjustment)==expected.property(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            }
            images=checkLive(edit->toPlainText()==heading+"\n\nBody",__LINE__)&&images;
        }
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        // Opening one inline span must leave its siblings rendered.
        edit->setPlainText("Start $x$ and **bold**\nplus $y$ end");settle();
        const auto formulaObject=QChar::ObjectReplacementCharacter;
        auto visible=output->toPlainText();
        caret=output->textCursor();caret.setPosition(visible.indexOf(formulaObject));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("$x$")&&!visible.contains("**")&&visible.count(formulaObject)==1,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(visible.indexOf("$x$")+1);
        caret.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
        output->setTextCursor(caret);output->insertPlainText("z");
        images=checkLive(edit->toPlainText()=="Start $z$ and **bold**\nplus $y$ end",__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("bold"));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("**bold**")&&visible.count(formulaObject)==2,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(visible.indexOf("end"));output->setTextCursor(caret);
        images=checkLive(!output->toPlainText().contains("**")&&output->toPlainText().count(formulaObject)==2,__LINE__)&&images;
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        // Native input-method commits and cross-paragraph paste must update source safely.
        QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("中文"));
        QApplication::sendEvent(output,&commit);
        images=checkLive(edit->toPlainText().startsWith(QString::fromUtf8("# 中文Heading")),__LINE__)&&images;
        output->selectAll();QApplication::clipboard()->setText("Pasted **text**\n\nSecond paragraph");output->paste();
        images=checkLive(edit->toPlainText()=="Pasted **text**\n\nSecond paragraph",__LINE__)&&images;
        // Backspace at the next unit's start must actually remove a newline.
        edit->setPlainText("First\n\nSecond");settle();
        caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));output->setTextCursor(caret);
        QKeyEvent backspace(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
        QApplication::sendEvent(output,&backspace);
        images=checkLive(edit->toPlainText()=="First\nSecond",__LINE__)&&images;
        edit->document()->setModified(false);
        // Exercise the actual toolbar/file-dialog/load path, not just setPlainText.
        QTemporaryDir files;
        QString filename=files.filePath(QString::fromUtf8("打开测试.md"));
        QFile fixture(filename);bool fixtureReady=fixture.open(QIODevice::WriteOnly);
        QByteArray contents=QString::fromUtf8("# 打开文档\n\n正文 **加粗** 与 $x^2$\n").toUtf8();
        if(app.arguments().size()>2){
            QFile documentFixture(app.arguments().at(2));
            bool readable=documentFixture.open(QIODevice::ReadOnly);
            images=checkLive(readable,__LINE__)&&images;
            if(readable)contents=documentFixture.readAll();
        }
        if(fixtureReady){fixtureReady=fixture.write(contents)==contents.size();fixture.close();}
        images=checkLive(fixtureReady,__LINE__)&&images;
        auto *openAction=live.findChild<QAction *>("openDocumentAction");
        openAction->trigger();
        auto *fileDialog=live.findChild<QFileDialog *>();
        images=checkLive(fileDialog!=nullptr,__LINE__)&&images;
        if(fileDialog){
            images=checkLive(fileDialog->isVisible(),__LINE__)&&images;
            fileDialog->selectFile(filename);
            edit->insertPlainText("Unsaved changes");
            images=checkLive(edit->document()->isModified(),__LINE__)&&images;
            bool discarded=false;
            QTimer::singleShot(0,&live,[&]{
                auto *prompt=live.findChild<QMessageBox *>();
                if(prompt&&prompt->button(QMessageBox::Discard)){
                    discarded=true;prompt->button(QMessageBox::Discard)->click();
                }
            });
            QMetaObject::invokeMethod(fileDialog,"accept",Qt::DirectConnection);
            settle();
            images=checkLive(discarded&&!edit->document()->isModified(),__LINE__)&&images;
            images=checkLive(edit->toPlainText()==QString::fromUtf8(contents)&&
                liveOutput->markdown()==edit->toPlainText()&&
                !output->toPlainText().isEmpty(),__LINE__)&&images;
        }
        edit->document()->setModified(false);
        // Measure visible layout for units after the first one, where Qt
        // may discard the first block format when inserting a fragment.
        {
            LiveEditor layoutProbe;
            layoutProbe.setFont(output->font());
            layoutProbe.resize(640,480);
            layoutProbe.show();
            for(const QString &middle:{QString("Body"),QString("# Heading"),QString("First row\nSecond row"),QString("Body **bold**"),QString(" \t")}){
                const QString original="Before\n\n"+middle+"\n\nAfter";
                layoutProbe.setSource(original);
                auto *doc=layoutProbe.document();
                auto top=[&](const QTextBlock &block){return doc->documentLayout()->blockBoundingRect(block).top();};
                const qreal before=top(doc->lastBlock());
                const qreal unitBefore=top(doc->findBlockByNumber(2));
                layoutProbe.setTextCursor(QTextCursor(doc->findBlockByNumber(2)));
                images=checkLive(before>unitBefore&&unitBefore>0&&qAbs(top(doc->lastBlock())-before)<1.0&&
                    qAbs(top(doc->findBlockByNumber(2))-unitBefore)<1.0&&layoutProbe.markdown()==original,__LINE__)&&images;
            }
        }
        {
            LiveEditor probe;
            probe.resize(640,480);
            probe.show();
            probe.setSource("Start $x$ and **bold**\nplus $y$ end");
            const QString visible=probe.toPlainText();
            caret=probe.textCursor();caret.setPosition(visible.indexOf(formulaObject));
            const auto leftEdge=probe.cursorRect(caret);
            caret.movePosition(QTextCursor::NextCharacter);
            const auto rightEdge=probe.cursorRect(caret);
            const QPoint hit(rightEdge.left()-2,leftEdge.center().y());
            QMouseEvent click(QEvent::MouseButtonPress,QPointF(hit),
                QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&click);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(hit),
                QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&release);
            images=checkLive(probe.toPlainText().contains("$x$")&&
                probe.toPlainText().count(formulaObject)==1,__LINE__)&&images;
        }
        QImage shot(view.size(),QImage::Format_ARGB32);shot.fill(QColor("#202027"));view.setStyleSheet("color:#ddd9e7;background:#202027;");view.render(&shot);shot.save(QDir::current().absoluteFilePath("math-smoke.png"));
        if(!images){qWarning()<<n<<errors;result=1;}
    }else{Window w;if(app.arguments().size()>1)w.load(app.arguments().at(1));w.show();result=app.exec();}
    tex::LaTeX::release();return result;
}




