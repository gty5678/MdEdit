#include <QtWidgets>
#include "latex.h"
#include "core/formula.h"
#include "platform/qt/graphic_qt.h"
#include <memory>
#include <functional>
#include <optional>

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
struct Span { int start, length; SpanKind kind; bool fenced=false; };
static bool escaped(const QString &s,int pos){int n=0;while(pos>0&&s[--pos]=='\\')++n;return n%2;}

static int quotePrefixLength(QStringView line,int depth){
    static const QRegularExpression prefix(R"( {0,3}>[ \t]?)");
    int offset=0;
    for(int i=0;i<depth;++i){
        const auto marker=prefix.matchView(line,offset,QRegularExpression::NormalMatch,
            QRegularExpression::AnchorAtOffsetMatchOption);
        if(!marker.hasMatch())return -1;
        offset=marker.capturedEnd();
    }
    return offset;
}

static int enclosingListIndent(const QString &source,int lineStart,int indent){
    static const QRegularExpression item(R"(^( *)(?:[-+*]|[0-9]{1,9}[.)])[ \t]+)");
    int end=lineStart-1;
    while(end>=0){
        const int start=end==0?0:source.lastIndexOf('\n',end-1)+1;
        const auto line=QStringView(source).mid(start,end-start);
        const auto marker=item.matchView(line);
        if(marker.hasMatch()&&marker.capturedEnd()<=indent)return marker.capturedEnd();
        int spaces=0;
        while(spaces<line.size()&&line[spaces]==' ')++spaces;
        if(!line.trimmed().isEmpty()&&spaces<indent)return 0;
        end=start-1;
    }
    return 0;
}

// One scanner defines the boundaries for both syntax highlighting and preview.
static QList<Span> scanSpans(const QString &s){
    QList<Span> spans;
    for(int i=0;i<s.size();){
        if(escaped(s,i)){++i;continue;}
        const QChar ch=s[i];
        if(ch=='`'||ch=='~'){
            int count=1;while(i+count<s.size()&&s[i+count]==ch)++count;
            int lineStart=i==0?0:int(s.lastIndexOf('\n',i-1))+1;
            const auto prefix=QStringView(s).mid(lineStart,i-lineStart);
            const int quoteDepth=count>=3?prefix.count('>'):0;
            const int quoteLength=quotePrefixLength(prefix,quoteDepth);
            const int listIndent=count>=3&&!quoteDepth&&!prefix.isEmpty()&&prefix.count(' ')==prefix.size()?
                enclosingListIndent(s,lineStart,prefix.size()):0;
            if(count>=3&&quoteLength>=0&&prefix.size()-quoteLength-listIndent<=3&&
               prefix.mid(quoteLength).count(' ')==prefix.size()-quoteLength){
                const int openingEnd=s.indexOf('\n',i+count);
                const auto info=QStringView(s).mid(i+count,openingEnd<0?-1:openingEnd-i-count);
                if(ch!='`'||!info.contains('`')){
                    int end=int(s.size());int next=openingEnd;
                    while(next>=0&&next+1<s.size()){
                        int start=next+1;next=int(s.indexOf('\n',start));
                        int lineEnd=next<0?int(s.size()):next;
                        auto line=QStringView(s).mid(start,lineEnd-start);
                        if(quoteDepth){
                            const int length=quotePrefixLength(line,quoteDepth);
                            if(length<0){end=start-1;break;}
                            line=line.mid(length);
                        }
                        int indent=0;while(indent<line.size()&&line[indent]==' ')++indent;
                        if(indent<listIndent&&!line.trimmed().isEmpty()){end=start-1;break;}
                        int run=0;while(indent+run<line.size()&&line[indent+run]==ch)++run;
                        if(indent-listIndent<=3&&run>=count&&line.mid(indent+run).trimmed().isEmpty()){end=lineEnd;break;}
                    }
                    spans.append({i,end-i,SpanKind::Code,true});i=end;continue;
                }
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

struct CodeBody {
    int start=0,end=0;
    QString language;
};

static CodeBody codeBody(const QString &text,int fenceStart=-1){
    if(fenceStart>=0&&text.mid(fenceStart,3)!="```"&&text.mid(fenceStart,3)!="~~~")fenceStart=-1;
    const auto spans=fenceStart>=0?QList<Span>{{fenceStart,int(text.size())-fenceStart,SpanKind::Code,true}}:scanSpans(text);
    for(const auto &span:spans){
        if(!span.fenced)continue;
        const int openingEnd=text.indexOf('\n',span.start);
        if(openingEnd<0)return {int(text.size()),int(text.size()),{}};
        int count=0;
        while(span.start+count<text.size()&&text[span.start+count]==text[span.start])++count;
        const QString language=text.mid(span.start+count,openingEnd-span.start-count).trimmed().section(' ',0,0).toLower();
        int end=span.start+span.length;
        const int closingStart=text.lastIndexOf('\n',end-1)+1;
        const auto closing=text.mid(closingStart,end-closingStart);
        const QRegularExpression closer("^[ >\\t]*"+QString(text[span.start])+"{"+QString::number(count)+",}[ \\t]*$");
        if(closingStart>openingEnd&&closer.match(closing).hasMatch())end=qMax(openingEnd+1,closingStart-1);
        return {openingEnd+1,end,language};
    }
    return {0,int(text.size()),{}};
}

// Small offline lexer shared by Live and full-source editing. Unknown
// languages stay plain; comments and strings take precedence over keywords.
static void colorCode(const QString &text,QString language,
        const std::function<void(int,int,const QColor &)> &apply){
    if(language=="c++"||language=="c"||language=="cc"||language=="h"||language=="hpp")language="cpp";
    if(language=="py")language="python";
    if(language=="js"||language=="ts"||language=="typescript"||language=="jsx"||language=="tsx")language="javascript";
    if(language=="sh"||language=="shell"||language=="zsh")language="bash";
    if(language=="tex")language="latex";
    const bool python=language=="python",shell=language=="bash",latex=language=="latex";
    const bool cLike=language=="cpp"||language=="javascript",json=language=="json";
    if(!python&&!shell&&!latex&&!cLike&&!json)return;
    static const QSet<QString> words=[] {
        const auto list=QString("alignas alignof auto bool break case catch char class const constexpr continue decltype default delete do double else enum explicit export extern false float for friend if inline int long namespace new noexcept nullptr operator private protected public register return short signed sizeof static static_assert struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while and as assert async await def del elif except False finally from global import in is lambda None nonlocal not or pass raise True with yield abstract arguments debugger extends function implements instanceof interface let null package super typeof var undefined of then fi esac done until select local echo").split(' ',Qt::SkipEmptyParts);
        return QSet<QString>(list.cbegin(),list.cend());
    }();
    const QColor keyword("#8959a8"),string("#718c00"),comment("#8e908c"),number("#c76b29"),command("#4271ae");
    for(int i=0;i<text.size();){
        const int start=i;
        if((cLike&&text.mid(i,2)=="//")||((python||shell)&&text[i]=='#')||(latex&&text[i]=='%'&&!escaped(text,i))){
            i=text.indexOf('\n',i);if(i<0)i=text.size();apply(start,i-start,comment);
        }else if(cLike&&text.mid(i,2)=="/*"){
            i=text.indexOf("*/",i+2);i=i<0?int(text.size()):i+2;apply(start,i-start,comment);
        }else if(latex&&text[i]=='\\'){
            ++i;while(i<text.size()&&text[i].isLetter())++i;if(i==start+1&&i<text.size())++i;apply(start,i-start,command);
        }else if(!latex&&(text[i]=='\"'||text[i]=='\''||(language=="javascript"&&text[i]=='`'))){
            const QChar quote=text[i];
            const int count=python&&text.mid(i,3)==QString(3,quote)?3:1;
            i+=count;
            while(i<text.size()){
                if(text[i]=='\\'){i=qMin(i+2,int(text.size()));continue;}
                if(text.mid(i,count)==QString(count,quote)){i+=count;break;}
                if(count==1&&quote!='`'&&text[i]=='\n')break;
                ++i;
            }
            apply(start,i-start,string);
        }else if(text[i].isDigit()&&(i==0||(!text[i-1].isLetterOrNumber()&&text[i-1]!='_'))){
            ++i;while(i<text.size()&&(text[i].isLetterOrNumber()||text[i]=='.'||text[i]=='_'))++i;apply(start,i-start,number);
        }else if(text[i].isLetter()||text[i]=='_'){
            ++i;while(i<text.size()&&(text[i].isLetterOrNumber()||text[i]=='_'))++i;
            if(words.contains(text.mid(start,i-start)))apply(start,i-start,keyword);
        }else ++i;
    }
}

class Highlighter : public QSyntaxHighlighter {
    bool spansDirty=true;
    QList<Span> spans;
    struct Token { int start,length; QColor color; };
    QList<Token> tokens;
    QHash<int,int> codeStates;
public:
    explicit Highlighter(QTextDocument *d):QSyntaxHighlighter(static_cast<QObject *>(d)){
        connect(d,&QTextDocument::contentsChange,this,[this]{spansDirty=true;});
        setDocument(d);
    }
    void highlightBlock(const QString &text) override {
        if(spansDirty){
            const QString source=document()->toPlainText();
            spans=scanSpans(source);tokens.clear();codeStates.clear();spansDirty=false;
            for(const auto &span:spans)if(span.fenced){
                const QString raw=source.mid(span.start,span.length);
                const auto body=codeBody(raw);
                codeStates.insert(span.start,int(qHash(raw)&0x7fffffff));
                colorCode(raw.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
                    tokens.append({span.start+body.start+start,length,color});
                });
            }
        }
        static const QList<QPair<QRegularExpression,QString>> rules={{QRegularExpression("^#{1,6} .*$"),"#623e9c"},{QRegularExpression("\\*\\*.+?\\*\\*"),"#825018"}};
        for(const auto &r:rules){auto matches=r.first.globalMatch(text);while(matches.hasNext()){auto m=matches.next();QTextCharFormat f;f.setForeground(QColor(r.second));setFormat(m.capturedStart(),m.capturedLength(),f);}}
        const int start=currentBlock().position(),end=start+int(text.size());int state=0;
        auto first=std::lower_bound(spans.cbegin(),spans.cend(),start,
            [](const Span &span,int position){return span.start+span.length<=position;});
        for(auto it=first;it!=spans.cend()&&it->start<=end;++it){
            const auto &span=*it;
            int from=qMax(start,span.start),to=qMin(end,span.start+span.length);
            if(to>from){QTextCharFormat f;f.setForeground(QColor(span.kind==SpanKind::Code?"#276746":"#14616b"));if(span.kind!=SpanKind::Code)f.setBackground(QColor("#e7f3f5"));setFormat(from-start,to-from,f);}
            if(span.start<=end&&span.start+span.length>end)state=span.fenced?codeStates.value(span.start):span.kind==SpanKind::Code?1:2;
        }
        for(const auto &token:tokens){
            if(token.start>end)break;
            const int from=qMax(start,token.start),to=qMin(end,token.start+token.length);
            if(to>from){QTextCharFormat format;format.setForeground(token.color);setFormat(from-start,to-from,format);}
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

static QString preserveLineBreaks(const QString &source){
    const auto spans=scanSpans(source);
    auto span=spans.cbegin();
    QString result;
    int start=0;
    for(int end=source.indexOf('\n');end>=0;end=source.indexOf('\n',start)){
        while(span!=spans.cend()&&span->start+span->length<=end)++span;
        const bool insideCode=span!=spans.cend()&&span->start<=end&&span->kind==SpanKind::Code;
        const auto line=QStringView(source).mid(start,end-start);
        result+=line;
        const bool hardBreak=end>start&&source[end-1]=='\\'&&!escaped(source,end-1);
        if(!insideCode&&!hardBreak&&!line.trimmed().isEmpty())result+="  ";
        result+='\n';
        start=end+1;
    }
    return result+source.mid(start);
}

static const QColor quoteBackground("#eee9f7");

static qreal bodyLineHeight(const QFont &font){
    return qCeil(QFontMetricsF(font).lineSpacing()*1.5);
}

static void setBodyLineHeight(QTextBlockFormat &format,const QFont &font){
    format.setLineHeight(bodyLineHeight(font),QTextBlockFormat::FixedHeight);
}

static bool isCodeBlock(const QTextBlockFormat &format){
    return format.hasProperty(QTextFormat::BlockCodeLanguage)||format.hasProperty(QTextFormat::BlockCodeFence);
}

static constexpr int formulaPaintHeight=QTextFormat::UserProperty+1;
static constexpr int formulaPaintAscent=QTextFormat::UserProperty+2;

class FormulaImageHandler : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
    QTextObjectInterface *imageHandler;
public:
    explicit FormulaImageHandler(QAbstractTextDocumentLayout *layout)
        :QObject(layout),imageHandler(layout->handlerForObject(QTextFormat::ImageObject)){
        layout->registerHandler(QTextFormat::ImageObject,this);
    }
    QSizeF intrinsicSize(QTextDocument *document,int pos,const QTextFormat &format)override{
        return imageHandler->intrinsicSize(document,pos,format);
    }
    void drawObject(QPainter *painter,const QRectF &rect,QTextDocument *document,int pos,const QTextFormat &format)override{
        QRectF painted=rect;
        if(format.hasProperty(formulaPaintAscent)){
            painted.moveTop(rect.bottom()-format.doubleProperty(formulaPaintAscent));
            painted.setHeight(format.doubleProperty(formulaPaintHeight));
        }
        imageHandler->drawObject(painter,painted,document,pos,format);
    }
};

// Use an explicit hierarchy: Qt's default H4-H6 share the body size.
static QTextCharFormat headingCharacters(int level,const QFont &base){
    static constexpr qreal scales[]={2.0,1.75,1.5,1.3,1.2,1.1};
    QFont font=base;
    const qreal scale=scales[qBound(1,level,6)-1];
    if(base.pointSizeF()>0)font.setPointSizeF(base.pointSizeF()*scale);
    else font.setPixelSize(qRound(base.pixelSize()*scale));
    font.setWeight(QFont::Bold);
    QTextCharFormat characters;characters.setFont(font);
    characters.setProperty(QTextFormat::FontSizeAdjustment,0);
    return characters;
}

static int renderMarkdown(QTextDocument *document,const QString &source,QStringList &errors,bool live=false){
    document->setIndentWidth(24);
    auto *layout=document->documentLayout();
    if(!live&&!dynamic_cast<FormulaImageHandler *>(layout->handlerForObject(QTextFormat::ImageObject)))
        new FormulaImageHandler(layout);
    QList<QPair<QString,QString>> math;
    document->setMarkdown(preserveLineBreaks(protectMath(source,math)));
    for(auto block=document->begin();block.isValid();block=block.next()){
        auto format=block.blockFormat();
        if(format.headingLevel()){
            QTextCursor heading(block);heading.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
            heading.mergeCharFormat(headingCharacters(format.headingLevel(),document->defaultFont()));
        }
        if(format.intProperty(QTextFormat::BlockQuoteLevel)>0){
            format.setBackground(quoteBackground);
            QTextCursor(block).setBlockFormat(format);
        }
    }
    int rendered=0;
    for(const auto &entry:math){
        auto cursor=document->find(entry.first);if(cursor.isNull())continue;
        try {
            const bool block=entry.second.startsWith("$$");
            const int delimiter=block?2:1;
            tex::Formula parsed(entry.second.mid(delimiter,entry.second.size()-delimiter*2).toStdWString());
            const QFontMetricsF textMetrics(block?document->defaultFont():
                cursor.charFormat().font().resolve(document->defaultFont()));
            tex::TeXRenderBuilder builder;
            builder.setStyle(block?tex::TexStyle::display:tex::TexStyle::text)
                .setTextSize(float(textMetrics.height()))
                .setForeground(0xff26232d).setTrueValues(true);
            // Natural width for inline math; display alignment belongs to its paragraph.
            std::unique_ptr<tex::TeXRender> formula(builder.build(parsed));
            const int padding=block?4:1;
            const int width=qMax(1,formula->getWidth()+2*padding);
            const int height=qMax(1,formula->getHeight()+2*padding);
            if(width>10000||height>10000)throw std::runtime_error("Formula too large");
            // Measure from the TeX baseline so descenders can paint below the text baseline.
            const qreal baseline=formula->getBaseline();
            const qreal ascent=padding+(qIsFinite(baseline)?qBound(0.0,baseline,1.0):0.0)*(height-2*padding);
            const qreal descent=height-ascent;
            // TeX glyphs sit visually low beside the body font, especially CJK text.
            const qreal lift=block?0.0:textMetrics.height()*0.22;
            const qreal scale=block?1.0:qMin(1.0,qMin(
                (textMetrics.ascent()-lift)/ascent,(textMetrics.descent()+lift)/descent));
            const qreal imageWidth=width*scale;
            const qreal imageHeight=height*scale;
            QImage image(QSize(qCeil(imageWidth*2),qCeil(imageHeight*2)),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.scale(image.width()/imageWidth,image.height()/imageHeight);
            painter.scale(scale,scale);
            painter.setRenderHint(QPainter::Antialiasing);
            tex::Graphics2D_qt graphics(&painter);formula->draw(graphics,padding,padding);painter.end();
            const QUrl url("math:/"+QUuid::createUuid().toString(QUuid::WithoutBraces));document->addResource(QTextDocument::ImageResource,url,image);
            QTextImageFormat format;format.setName(url.toString());format.setWidth(imageWidth);format.setHeight(imageHeight);format.setVerticalAlignment(QTextCharFormat::AlignBaseline);
            if(!block){
                const qreal imageAscent=ascent*scale+lift;
                format.setHeight(imageAscent);
                format.setProperty(formulaPaintAscent,imageAscent);
                format.setProperty(formulaPaintHeight,imageHeight);
            }
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
        QPainter painter(numbers);painter.fillRect(event->rect(),QColor("#ffffff"));
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
                    painter.setPen(number==current?QColor("#623e9c"):QColor("#706978"));
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
    struct Region {
        int sourceStart, sourceEnd, start, end, contentEnd=-1;
        bool inlinePart=false, continuesBefore=false, continuesAfter=false;
        std::optional<QTextBlockFormat> listItemFormat;
        bool codeBlock=false;
        int codeFenceStart=-1;
    };
    QList<Region> regions;
    QString source;
    int active=-1;
    bool rebuilding=false;
    bool movingCursor=false;
    bool sourceExposed=false;
    QString pressedLink;
    QPoint linkPressPosition;
    QString clickableAnchorAt(const QPoint &point) const{
        const QString anchor=anchorAt(point);
        const QUrl url(anchor);
        if((url.isRelative()&&anchor.startsWith('#')&&!url.fragment().isEmpty())||
           ((url.scheme()=="http"||url.scheme()=="https")&&url.isValid()&&!url.host().isEmpty()))
            return anchor;
        return {};
    }
    QString oldText;
    QCache<QString,QTextDocument> renderedMarkdown{128};
    QFont renderedFont;
    QUrl renderedBaseUrl;
public:
    std::function<void(const QString &)> sourceChanged;
    std::function<void(bool)> history;
    explicit LiveEditor(){
        document()->setIndentWidth(24);
        new FormulaImageHandler(document()->documentLayout());
        setReadOnly(false);setAcceptRichText(false);setUndoRedoEnabled(false);setAcceptDrops(false);setContextMenuPolicy(Qt::NoContextMenu);
        viewport()->setMouseTracking(true);
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
    QPair<int,int> sourceSelection()const{
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1)
            return caret.anchor()<caret.position()?qMakePair(0,int(source.size())):qMakePair(int(source.size()),0);
        return {sourcePosition(caret.anchor()),sourcePosition(caret.position())};
    }
    void restoreSourceSelection(int anchor,int position){
        anchor=qBound(0,anchor,int(source.size()));position=qBound(0,position,int(source.size()));
        if(anchor==position)rebuild(-1,0,position);
        else exposeSource(anchor,position);
        ensureCursorVisible();
    }
    int sourceLineCount()const override{return source.count('\n')+1;}
    int lineNumberAt(const QTextCursor &cursor)const override{
        return source.left(sourcePosition(cursor.position())).count('\n')+1;
    }
    void rebuild(int chosen,int offset,int forcedTarget=-1){
        rebuilding=true;int scroll=verticalScrollBar()->value();
        sourceExposed=false;
        const int caretY=cursorRect().top();
        if(renderedFont!=font()||renderedBaseUrl!=document()->baseUrl()){
            renderedMarkdown.clear();
            renderedFont=font();
            renderedBaseUrl=document()->baseUrl();
        }
        // Give each source blank line its own editable block. Keep fenced
        // code and display formulas intact for their respective renderers.
        const auto spans=scanSpans(source);
        QList<QPair<int,int>> units;
        int begin=0;
        static const QRegularExpression blankLine("(?:^|(?<=\\n))[ \\t]*\\n");
        auto matches=blankLine.globalMatch(source);
        auto span=spans.cbegin();
        while(matches.hasNext()){
            const auto match=matches.next();
            const int start=match.capturedStart();
            while(span!=spans.cend()&&span->start+span->length<=start)++span;
            if(span!=spans.cend()&&span->start<=start)continue;
            if(begin<start)units.append({begin,start});
            units.append({start,match.capturedEnd()});
            begin=match.capturedEnd();
        }
        if(begin<source.size())units.append({begin,int(source.size())});
        if(source.isEmpty()||source.endsWith('\n'))units.append({int(source.size()),int(source.size())});
        // Split text and headings when each source line can render independently.
        struct ListItem { QString listSource; int blockNumber; };
        QHash<int,ListItem> listItems;
        QList<QPair<int,int>> lineUnits;
        QSet<int> continuations;
        QSet<int> codeUnits;
        QHash<int,int> fenceOffsets;
        QList<QPair<int,int>> fenceUnits;
        for(const auto &code:spans){
            if(!code.fenced)continue;
            const int start=code.start==0?0:source.lastIndexOf('\n',code.start-1)+1;
            const int end=code.start+code.length;
            fenceUnits.append({start,end<source.size()?end+1:end});
            codeUnits.insert(start);
            fenceOffsets.insert(start,code.start-start);
        }
        QList<QPair<int,int>> splitUnits;
        auto fence=fenceUnits.cbegin();
        for(const auto &unit:units){
            int start=unit.first;
            for(;fence!=fenceUnits.cend()&&fence->first<unit.second;++fence){
                if(start<fence->first)splitUnits.append({start,fence->first});
                splitUnits.append(*fence);
                start=fence->second;
            }
            if(start<unit.second||unit.first==unit.second)splitUnits.append({start,unit.second});
        }
        for(qsizetype index=0;index<splitUnits.size();++index){
            auto unit=splitUnits[index];
            QString text=source.mid(unit.first,unit.second-unit.first);
            if(text.endsWith('\n'))text.chop(1);
            if(!codeUnits.contains(unit.first)&&(text.startsWith("    ")||text.startsWith('\t')||
                (text.startsWith(' ')&&text.left(4).contains('\t')))){
                const auto *rendered=renderedDocument(text,false);
                auto block=rendered->begin();
                int end=0;
                while(block.isValid()&&isCodeBlock(block.blockFormat())){
                    const int newline=text.indexOf('\n',end);
                    end=newline<0?int(text.size()):newline+1;
                    block=block.next();
                }
                if(end>0){
                    codeUnits.insert(unit.first);
                    if(end<text.size()){
                        splitUnits.insert(index+1,{unit.first+end,unit.second});
                        unit.second=unit.first+end;
                        text.truncate(end-1);
                    }
                }
            }
            const auto lines=text.split('\n');
            if(codeUnits.contains(unit.first)||(lines.size()<2&&!listMarker().match(text).hasMatch())||std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
                return span.start>=unit.first&&span.start<unit.second&&
                    (span.kind==SpanKind::DisplayMath||QStringView(source).mid(span.start,span.length).contains('\n'));
            })){
                lineUnits.append(unit);
                continue;
            }
            const auto *rendered=renderedDocument(text,false);
            {
                const auto previousUnitCount=lineUnits.size();
                QList<QPair<int,int>> items;
                QHash<int,ListItem> renderedItems;
                int line=0;
                int start=unit.first;
                for(auto block=rendered->begin();block.isValid();block=block.next()){
                    if(line>=lines.size())break;
                    if(block.textList()){
                        if(!listMarker().match(lines[line]).hasMatch())break;
                        renderedItems.insert(start,{text,block.blockNumber()});
                    }else if(!isTextBlock(block))break;
                    const int next=line+block.text().count(QChar::LineSeparator)+1;
                    if(next>lines.size())break;
                    int end=start;
                    for(;line<next;++line)end+=lines[line].size()+1;
                    items.append({start,qMin(end,unit.second)});
                    start=end;
                    if(!block.next().isValid()&&line==lines.size()&&!renderedItems.isEmpty()){
                        listItems.insert(renderedItems);
                        lineUnits.append(items);
                    }
                }
                if(lineUnits.size()>previousUnitCount)continue;
            }
            bool independent=true;
            for(auto block=rendered->begin();block.isValid();block=block.next()){
                if(!isTextBlock(block)){independent=false;break;}
            }
            if(!independent){
                lineUnits.append(unit);
                continue;
            }
            QStringList renderedLines;
            QList<bool> headings;
            for(const auto &line:lines){
                const auto *renderedLine=renderedDocument(line,false);
                renderedLines.append(renderedLine->toPlainText());
                headings.append(renderedLine->begin().blockFormat().headingLevel()>0);
            }
            if(renderedLines.join('\n')!=rendered->toPlainText()){
                lineUnits.append(unit);
                continue;
            }
            int start=unit.first;
            for(qsizetype i=0;i<lines.size();++i){
                const int end=i+1==lines.size()?unit.second:start+lines[i].size()+1;
                lineUnits.append({start,end});
                if(i+1<lines.size()&&!headings[i]&&!headings[i+1])continuations.insert(end);
                start=end;
            }
        }
        units.swap(lineUnits);
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
            if(codeUnits.contains(unit.first)){
                const auto body=codeBody(text,fenceOffsets.value(unit.first,-1));
                out.setBlockFormat(codeBlockFormat());
                const auto characters=codeCharacters();
                out.setBlockCharFormat(characters);
                out.insertText(text,characters);
                if(!editing&&body.start>0){
                    // Keep fence rows and their layout, hiding only their ink.
                    // The same source offsets then work in both editor states.
                    auto hide=[&](int start,int end){
                        QTextCursor fence(document());fence.setPosition(from+start);
                        fence.setPosition(from+end,QTextCursor::KeepAnchor);
                        QTextCharFormat invisible;invisible.setForeground(Qt::transparent);
                        fence.mergeCharFormat(invisible);
                    };
                    hide(0,body.start);
                    hide(body.end,int(text.size()));
                }
                colorCode(text.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
                    QTextCursor token(document());
                    token.setPosition(from+body.start+start);
                    token.setPosition(token.position()+length,QTextCursor::KeepAnchor);
                    QTextCharFormat format;format.setForeground(color);token.mergeCharFormat(format);
                });
                if(editing)active=i;
                regions.append({unit.first,unit.second,from,out.position(),contentEnd,false,false,false,{},true,fenceOffsets.value(unit.first,-1)});
                continue;
            }
            const bool continuesBefore=continuations.contains(unit.first);
            const bool continuesAfter=continuations.contains(unit.second)&&unit.first!=unit.second;
            const auto item=listItems.constFind(unit.first);
            std::optional<QTextBlockFormat> listItemFormat;
            int bodyOffset=0;
            auto pieces=inlinePieces(text);
            if(item!=listItems.cend()&&!text.contains('\n')){
                bodyOffset=listMarker().match(text).capturedLength();
                const QString body=text.mid(bodyOffset);
                pieces=inlinePieces(body);
                // Plain list text is also an independent editing region.
                // Keep complex Markdown on the parser's existing fallback.
                if(pieces.isEmpty()&&renderedDocument(body,false,true)->toPlainText()==body)
                    pieces.append({int(body.size()),false});
            }
            if(!pieces.isEmpty()){
                const bool editingPrefix=editing&&bodyOffset>0&&target<unit.first+bodyOffset;
                QTextDocument body;body.setMarkdown("Body");
                auto format=body.begin().blockFormat();
                if(item!=listItems.cend()){
                    const auto *rendered=renderedDocument(item->listSource,false);
                    const auto block=rendered->findBlockByNumber(item->blockNumber);
                    format=block.blockFormat();
                    format.setObjectIndex(-1);
                    out.setBlockFormat(format);
                    auto listFormat=block.textList()->format();
                    listFormat.setStart(listFormat.start()+block.textList()->itemNumber(block));
                    out.createList(listFormat);
                    format=out.blockFormat();
                    if(editingPrefix){
                        // Replace the painted bullet with its editable source,
                        // keeping the list body at the same horizontal position.
                        format.setObjectIndex(-1);
                        format.clearProperty(QTextFormat::BlockMarker);
                        format.setLeftMargin(format.leftMargin()+listFormat.indent()*document()->indentWidth());
                        format.setTextIndent(-QFontMetricsF(font()).horizontalAdvance(text.left(bodyOffset)));
                    }
                }
                setBodyLineHeight(format,font());
                if(continuesBefore)format.setTopMargin(0);
                if(continuesAfter)format.setBottomMargin(0);
                out.setBlockFormat(format);
                QTextCharFormat normal;normal.setFont(font());
                if(editingPrefix){
                    active=regions.size();
                    const int start=out.position();
                    out.insertText(text.left(bodyOffset),normal);
                    regions.append({unit.first,unit.first+bodyOffset,start,out.position(),unit.first+bodyOffset,true});
                }
                int begin=bodyOffset;
                for(const auto &piece:pieces){
                    const int end=bodyOffset+piece.first;
                    const int start=out.position();
                    const bool selected=editing&&!editingPrefix&&target>=unit.first+begin&&
                        (target<unit.first+end||end==text.size());
                    const QString raw=text.mid(begin,end-begin);
                    if(selected||!piece.second){
                        if(selected)active=regions.size();
                        QString displayed=raw;
                        displayed.replace('\n',QChar::LineSeparator);
                        out.insertText(displayed,normal);
                    }else{
                        const auto *rendered=renderedDocument(raw,false,true);
                        for(auto it=rendered->begin().begin();!it.atEnd();++it){
                            const auto fragment=it.fragment();
                            auto format=fragment.charFormat();
                            if(format.isImageFormat()){
                                const QUrl url(format.toImageFormat().name());
                                document()->addResource(QTextDocument::ImageResource,url,
                                    rendered->resource(QTextDocument::ImageResource,url));
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
                if(item!=listItems.cend()){
                    const auto *rendered=renderedDocument(item->listSource,false);
                    const auto block=rendered->findBlockByNumber(item->blockNumber);
                    listItemFormat=block.blockFormat();
                    listItemFormat->setObjectIndex(-1);
                    listItemFormat->clearProperty(QTextFormat::BlockMarker);
                }
            }else if(text.trimmed().isEmpty()){
                QTextCharFormat f;f.setFont(font());out.setBlockCharFormat(f);
            }else if(item!=listItems.cend()){
                const auto *rendered=renderedDocument(item->listSource,false);
                const auto block=rendered->findBlockByNumber(item->blockNumber);
                auto blockFormat=block.blockFormat();
                blockFormat.setObjectIndex(-1);
                out.setBlockFormat(blockFormat);
                auto listFormat=block.textList()->format();
                listFormat.setStart(listFormat.start()+block.textList()->itemNumber(block));
                out.createList(listFormat);
                for(auto it=block.begin();!it.atEnd();++it){
                    const auto fragment=it.fragment();
                    const auto format=fragment.charFormat();
                    if(format.isImageFormat()){
                        const QUrl url(format.toImageFormat().name());
                        document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                    }
                    out.insertText(fragment.text(),format);
                }
            }else{
                const auto *rendered=renderedDocument(text,isMultilineParagraph(text));
                for(auto block=rendered->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
                    auto format=it.fragment().charFormat();if(format.isImageFormat()){
                        QUrl url(format.toImageFormat().name());document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                    }
                }
                out.insertFragment(QTextDocumentFragment(rendered));
                // Qt can leave the empty destination block before a list.
                const auto destination=document()->findBlock(from);
                const auto firstItem=destination.next();
                if(rendered->begin().textList()&&destination.text().isEmpty()&&
                   !destination.textList()&&firstItem.isValid()&&firstItem.textList()){
                    const auto itemFormat=firstItem.blockFormat();
                    QTextCursor start(firstItem);
                    start.deletePreviousChar();
                    start.setBlockFormat(itemFormat);
                }
                // Qt can keep the destination block format for the first
                // paragraph of a fragment inserted into an empty block.
                QTextCursor first(document()->findBlock(from));
                auto format=rendered->begin().blockFormat();
                if(continuesBefore)format.setTopMargin(0);
                if(continuesAfter)format.setBottomMargin(0);
                if(format.objectIndex()<0&&first.blockFormat().objectIndex()<0)
                    first.setBlockFormat(format);
            }
            regions.append({unit.first,unit.second,from,out.position(),contentEnd,false,continuesBefore,continuesAfter,listItemFormat});
        }
        styleActiveSource();
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));setTextCursor(caret);}
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);
        if(forcedTarget>=0)ensureCursorVisible();
        else if(target>=0)verticalScrollBar()->setValue(scroll+cursorRect().top()-caretY);
        rebuilding=false;refreshLineNumbers();
    }
protected:
    void mouseMoveEvent(QMouseEvent *event)override{
        QTextBrowser::mouseMoveEvent(event);
        if(event->buttons()==Qt::NoButton){
            const bool overLink=!clickableAnchorAt(event->position().toPoint()).isEmpty();
            viewport()->setCursor(overLink?Qt::PointingHandCursor:Qt::IBeamCursor);
        }
    }
    void mousePressEvent(QMouseEvent *event)override{
        int formulaRegion=-1;
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            const auto point=event->position().toPoint();
            const QString anchor=clickableAnchorAt(point);
            if(!anchor.isEmpty()){
                pressedLink=anchor;
                linkPressPosition=point;
                event->accept();
                return;
            }
            const int hit=cursorForPosition(point).position();
            const auto block=document()->findBlock(hit);
            if(block.textList()){
                const QRect edge=cursorRect(QTextCursor(block));
                const int at=sourcePosition(block.position());
                const int lineStart=at==0?0:source.lastIndexOf('\n',at-1)+1;
                const auto marker=listMarker().match(source.mid(lineStart).section('\n',0,0));
                const qreal width=qMax(document()->indentWidth(),QFontMetricsF(font()).horizontalAdvance(marker.captured())+8);
                if(marker.hasMatch()&&point.y()>=edge.top()&&point.y()<=edge.bottom()&&
                    point.x()<edge.left()&&point.x()>=edge.left()-width){
                    rebuild(-1,0,lineStart+marker.captured().indexOf(QRegularExpression("[-+*0-9]")));
                    event->accept();
                    return;
                }
            }
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
    void mouseReleaseEvent(QMouseEvent *event)override{
        if(event->button()==Qt::LeftButton&&!pressedLink.isEmpty()){
            const QString anchor=pressedLink;
            pressedLink.clear();
            const auto point=event->position().toPoint();
            event->accept();
            if(event->modifiers()==Qt::NoModifier&&anchorAt(point)==anchor&&
               (point-linkPressPosition).manhattanLength()<QApplication::startDragDistance())
                emit anchorClicked(QUrl(anchor));
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }
    void focusInEvent(QFocusEvent *event)override{
        QTextBrowser::focusInEvent(event);
        if(event->reason()!=Qt::ActiveWindowFocusReason&&active<0)activateCaretRegion();
    }
    void focusOutEvent(QFocusEvent *event)override{
        QTextBrowser::focusOutEvent(event);
        // Switching windows should leave the editing region and selection intact.
        if(event->reason()!=Qt::ActiveWindowFocusReason&&active>=0)rebuild(-1,0);
    }
    void keyPressEvent(QKeyEvent *event)override{
        const bool deleting=event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete||event->matches(QKeySequence::Cut);
        const int scrollBeforeEdit=verticalScrollBar()->value();
        const bool newline=event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter;
        if(newline&&!(event->modifiers()&~(Qt::KeypadModifier|Qt::ShiftModifier))){
            const auto cursor=textCursor();
            const bool all=cursor.hasSelection()&&cursor.selectionStart()==0&&
                cursor.selectionEnd()==document()->characterCount()-1;
            const int start=all?0:sourcePosition(cursor.selectionStart());
            const int end=all?int(source.size()):sourcePosition(cursor.selectionEnd());
            int replaceStart=start;
            QString insertion="\n";
            if(!cursor.hasSelection()&&!(event->modifiers()&Qt::ShiftModifier)){
                const int lineStart=start==0?0:source.lastIndexOf('\n',start-1)+1;
                int lineEnd=source.indexOf('\n',start);if(lineEnd<0)lineEnd=source.size();
                const QString line=source.mid(lineStart,lineEnd-lineStart);
                bool protectedBlock=false;
                for(const auto &span:scanSpans(source))
                    if(span.start<=start&&start<span.start+span.length&&
                       (span.fenced||span.kind==SpanKind::DisplayMath))protectedBlock=true;
                static const QRegularExpression item(R"(^([ \t]*)([-+*]|([0-9]{1,9})([.)]))([ \t]+)(?:\[([ xX])\]([ \t]+))?)");
                const auto match=item.match(line);
                if(!protectedBlock&&match.hasMatch()&&start>=lineStart+match.capturedEnd()){
                    if(line.mid(match.capturedEnd()).trimmed().isEmpty()){
                        replaceStart=lineStart;insertion.clear();
                    }else{
                        QString marker=match.captured(2);
                        if(!match.captured(3).isEmpty())marker=QString::number(match.captured(3).toLongLong()+1)+match.captured(4);
                        insertion+=match.captured(1)+marker+match.captured(5);
                        if(!match.captured(6).isEmpty())insertion+="[ ]"+match.captured(7);
                    }
                }
            }
            source.replace(replaceStart,qMax(0,end-replaceStart),insertion);
            rebuild(-1,0,replaceStart+insertion.size());
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
        if(event->key()==Qt::Key_Left&&event->modifiers()==Qt::NoModifier&&!textCursor().hasSelection()&&
            textCursor().atBlockStart()&&textCursor().block().textList()){
            const int at=sourcePosition(textCursor().position());
            const int lineStart=at==0?0:source.lastIndexOf('\n',at-1)+1;
            const auto marker=listMarker().match(source.mid(lineStart).section('\n',0,0));
            if(marker.hasMatch()&&at==lineStart+marker.capturedLength()){
                rebuild(-1,0,at-1);event->accept();return;
            }
        }
        if(!textCursor().hasSelection()){
            int pos=textCursor().position();
            if(event->key()==Qt::Key_Backspace&&pos==regions[active].start&&regions[active].sourceStart>0){
                const int at=regions[active].sourceStart;
                exposeSource(at,at);
            }
            if(event->key()==Qt::Key_Delete&&pos==regions[active].end&&active+1<regions.size()){
                const int at=regions[active].sourceStart+pos-regions[active].start;
                exposeSource(at,at);
            }
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
        // Cross-region edits temporarily use the full source so Qt can apply
        // its usual deletion and selection rules across Markdown boundaries.
        if(sourceExposed)rebuild(-1,0,textCursor().position());
        else if(toPlainText()==oldText)activateCaretRegion();
        if(deleting){
            // Exposing source and restoring preview can both scroll the cursor
            // into view using temporary geometry. Keep the viewport anchored
            // to the final layout instead of inheriting those scroll changes.
            verticalScrollBar()->setValue(scrollBeforeEdit);
            const QRect caret=cursorRect();
            if(caret.top()<0||caret.bottom()>=viewport()->height())ensureCursorVisible();
        }
    }
    void insertFromMimeData(const QMimeData *mime)override{prepareSelection();insertPlainText(mime->text());}
    void inputMethodEvent(QInputMethodEvent *event)override{
        prepareSelection();
        QTextBrowser::inputMethodEvent(event);
    }
private:
    QTextCharFormat codeCharacters()const{
        auto codeFont=font();
        codeFont.setFamily(QString::fromLatin1(editorFont));
        codeFont.setStyleHint(QFont::Monospace);
        QTextCharFormat characters;
        characters.setFont(codeFont);
        characters.setForeground(QColor("#37474f"));
        return characters;
    }
    QTextBlockFormat codeBlockFormat()const{
        QTextBlockFormat format;
        setBodyLineHeight(format,font());
        format.setBackground(QColor("#f3f5f4"));
        return format;
    }
    static QTextCharFormat headingStyle(int level,const QFont &font,QTextBlockFormat &format){
        QTextDocument reference;
        reference.setDefaultFont(font);
        reference.setMarkdown(QString(level,'#')+" Heading");
        const auto heading=reference.begin();
        format=heading.blockFormat();
        const auto characters=headingCharacters(level,font);
        format.setLineHeight(qCeil(QFontMetricsF(characters.font()).lineSpacing()),QTextBlockFormat::FixedHeight);
        return characters;
    }
    static bool isStandaloneImage(const QTextBlock &block){
        if(block.text().trimmed()!=QString(QChar::ObjectReplacementCharacter))return false;
        for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(format.isImageFormat())return !format.hasProperty(formulaPaintAscent);
        }
        return false;
    }
    static void fitInlineImages(const QTextBlock &block,QTextDocument *document){
        const qreal ascent=qFloor(QFontMetricsF(document->defaultFont()).ascent());
        auto *handler=document->documentLayout()->handlerForObject(QTextFormat::ImageObject);
        const QString text=block.text();
        QTextCursor cursor(block);
        for(int offset=text.indexOf(QChar::ObjectReplacementCharacter);offset>=0;
            offset=text.indexOf(QChar::ObjectReplacementCharacter,offset+1)){
            const int position=block.position()+offset;
            cursor.setPosition(position);
            cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            auto image=cursor.charFormat().toImageFormat();
            if(!image.isValid()||image.hasProperty(formulaPaintAscent))continue;
            const QSizeF size=handler->intrinsicSize(document,position,image);
            if(size.height()<=ascent)continue;
            image.setWidth(size.width()*ascent/size.height());
            image.setHeight(ascent);
            cursor.setCharFormat(image);
        }
    }
    const QTextDocument *renderedDocument(const QString &text,bool joinedParagraph,bool inlinePart=false){
        // Inline pieces and paragraphs use different base URLs and block formatting.
        const QString key=(inlinePart?"i":joinedParagraph?"j":"p")+text;
        if(auto *cached=renderedMarkdown.object(key))return cached;
        auto *rendered=new QTextDocument;
        rendered->setDefaultFont(renderedFont);
        if(!inlinePart)rendered->setBaseUrl(renderedBaseUrl);
        QStringList errors;
        renderMarkdown(rendered,text,errors,true);
        for(auto block=rendered->begin();block.isValid();block=block.next()){
            auto format=block.blockFormat();
            if(!isCodeBlock(format)&&!format.alignment().testFlag(Qt::AlignHCenter)&&!isStandaloneImage(block)){
                if(format.headingLevel()){
                    QTextBlockFormat heading;
                    headingStyle(format.headingLevel(),renderedFont,heading);
                    format.setLineHeight(heading.lineHeight(),QTextBlockFormat::FixedHeight);
                }else setBodyLineHeight(format,renderedFont);
                if(block.text().contains(QChar::ObjectReplacementCharacter))fitInlineImages(block,rendered);
            }
            if(joinedParagraph){
                if(block.previous().isValid())format.setTopMargin(0);
                if(block.next().isValid())format.setBottomMargin(0);
            }
            QTextCursor(block).setBlockFormat(format);
        }
        renderedMarkdown.insert(key,rendered);
        return rendered;
    }
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
        static const QRegularExpression blockMarker(R"((?:^|\n)(?: {0,3}(?:#{1,6}(?: |$)|>|[-+*](?: |$)|\d+[.)] )| {4}|\t))");
        if(blockMarker.match(text).hasMatch())return {};

        if(text.contains('\n')&&!isMultilineParagraph(text))return {};
        QList<QPair<int,int>> tokens;
        for(const auto &span:scanSpans(text)){
            if(span.kind==SpanKind::DisplayMath)return {};
            if(span.kind==SpanKind::Code&&text.mid(span.start,span.length).contains('\n'))return {};
            tokens.append({span.start,span.start+span.length});
        }
        static const QRegularExpression emphasis(
            R"((?<![\\*])(\*{1,3})(?!\*)([^*\n]+)\1(?!\*)|(?<![\\~])(~~)(?!~)([^~\n]+)\3(?!~))");
        auto matches=emphasis.globalMatch(text);
        while(matches.hasNext()){
            const auto match=matches.next();
            const bool insideToken=std::any_of(tokens.begin(),tokens.end(),[&](const auto &token){
                return match.capturedStart()>=token.first&&match.capturedEnd()<=token.second;
            });
            if(insideToken)continue;
            const auto inner=match.captured(match.capturedStart(2)>=0?2:4);
            if(inner.front().isSpace()||inner.back().isSpace())continue;
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
    static bool isTextBlock(const QTextBlock &block){
        const auto format=block.blockFormat();
        return !block.textList()&&!QTextCursor(block).currentTable()&&!format.indent()&&
            !isCodeBlock(format)&&
            !format.hasProperty(QTextFormat::BlockQuoteLevel)&&!format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    }
    static const QRegularExpression &listMarker(){
        static const QRegularExpression marker(R"(^[ \t]*(?:[-+*]|[0-9]{1,9}[.)])(?:[ \t]+|$)(?:\[[ xX]\][ \t]+)?)");
        return marker;
    }
    static bool isMultilineParagraph(const QString &text){
        if(!text.contains('\n'))return false;
        for(const auto &span:scanSpans(text))
            if(span.kind==SpanKind::DisplayMath||text.mid(span.start,span.length).contains('\n'))return false;
        // A terminal backslash is already a Markdown hard break.
        if(text.contains("\\\n"))return false;
        QTextDocument reference;reference.setMarkdown(text);
        const auto block=reference.begin();
        return reference.blockCount()==1&&isTextBlock(block)&&!block.blockFormat().headingLevel();
    }
    void styleActiveSource(){
        if(active<0||regions[active].inlinePart)return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const auto &region=regions[active];
        const QString raw=source.mid(region.sourceStart,region.end-region.start);
        const auto body=codeBody(raw,region.codeFenceStart);
        const bool fenced=body.start>0;
        const bool joinedParagraph=!region.codeBlock&&!fenced&&isMultilineParagraph(raw);
        QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
        static const QRegularExpression heading("^ {0,3}(#{1,6})(?:[ \t]+|$)");
        static const QRegularExpression quote("^ {0,3}>");
        for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next()){
            QTextCharFormat characters;characters.setFont(font());
            QTextBlockFormat paragraph=block.text().trimmed().isEmpty()?QTextBlockFormat():bodyStyle.begin().blockFormat();
            if(region.listItemFormat){
                paragraph=*region.listItemFormat;
                if(block.position()>region.start)paragraph.setTopMargin(0);
                if(block.next().isValid()&&block.next().position()<=region.end)paragraph.setBottomMargin(0);
            }
            if(region.continuesBefore)paragraph.setTopMargin(0);
            if(region.continuesAfter)paragraph.setBottomMargin(0);
            if(joinedParagraph){
                if(block.position()>region.start)paragraph.setTopMargin(0);
                if(block.next().isValid()&&block.next().position()<=region.end)paragraph.setBottomMargin(0);
            }
            if(quote.match(block.text()).hasMatch())paragraph.setBackground(quoteBackground);
            const auto match=heading.match(block.text());
            if(!block.text().trimmed().isEmpty())setBodyLineHeight(paragraph,font());
            if(region.codeBlock||fenced){
                paragraph=codeBlockFormat();
                characters=codeCharacters();
            }else if(match.hasMatch()){
                // Ask Qt for its preview heading style, then apply it to the
                // whole source line, including the visible Markdown markers.
                characters.merge(headingStyle(match.capturedLength(1),font(),paragraph));
            }
            QTextCursor line(block);line.setPosition(block.position()+block.text().size(),QTextCursor::KeepAnchor);
            line.setBlockFormat(paragraph);line.setBlockCharFormat(characters);line.setCharFormat(characters);
        }
        if(region.codeBlock||fenced)colorCode(raw.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
            QTextCursor token(document());token.setPosition(region.start+body.start+start);
            token.setPosition(token.position()+length,QTextCursor::KeepAnchor);
            QTextCharFormat format;format.setForeground(color);token.mergeCharFormat(format);
        });
    }
    int sourcePosition(int pos)const{
        const int i=regionAt(pos);
        if(i>=0){const auto &r=regions[i];
            if(i==active)return r.sourceStart+pos-r.start;
            if(r.codeBlock)return r.sourceStart+pos-r.start;
            if(r.start==r.end)return r.sourceStart;
            if(pos==r.end)return r.contentEnd>=0?r.contentEnd:r.sourceEnd;
            QString visible=oldText.mid(r.start,r.end-r.start);int at=r.sourceStart;
            // Walk one character ahead to skip leading Markdown delimiters too.
            for(int j=0;j<=pos-r.start&&j<visible.size();++j){
                if(j==0||visible[j-1]=='\n'){
                    const auto block=document()->findBlock(r.start+j);
                    if(block.textList()&&block.position()==r.start+j){
                        const auto match=listMarker().match(source.mid(at,r.sourceEnd-at).section('\n',0,0));
                        if(match.hasMatch())at+=match.capturedLength();
                    }
                }
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
        sourceExposed=true;
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
    QStackedWidget *screens=new QStackedWidget;
    QToolBar *bar;
    QAction *previewAction;
    QTimer timer,autosave;
    QString path;
public:
    Window(){
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
                font-size: 15px;
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
        editor->setFrameShape(QFrame::NoFrame);editor->setFont(QFont(editorFont,13));editor->setTabStopDistance(32);editor->document()->setDocumentMargin(32);new Highlighter(editor->document());
        preview->setFrameShape(QFrame::NoFrame);preview->setFont(QFont(previewFont,12));preview->document()->setDocumentMargin(40);preview->setOpenLinks(false);
        pages->addWidget(editor);pages->addWidget(preview);pages->setCurrentIndex(1);
        screens->addWidget(welcome);screens->addWidget(pages);setCentralWidget(screens);
        setStyleSheet(R"(QMainWindow,QWidget{background:#ffffff;color:#26232d;}QToolBar{background:#ffffff;border:0;border-bottom:1px solid #dedbe4;padding:8px;spacing:8px;}QToolButton{padding:6px 16px;border:0;border-radius:5px;}QToolButton:hover,QToolButton:checked{background:#eae2f7;color:#482775;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#e3d7f5;selection-color:#26232d;}QScrollBar:vertical{background:#ffffff;width:10px;}QScrollBar::handle:vertical{background:#a9a2b2;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#26232d;background:#f2eef7;})");
        timer.setSingleShot(true);timer.setInterval(150);
        autosave.setSingleShot(true);autosave.setInterval(600);
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
        editor->document()->setModified(false);
        render();open->setFocus();
    }
    void updateTitle(){
        const bool modified=editor->document()->isModified();
        if(path.isEmpty()){setWindowTitle("MdEdit");return;}
        const QString name=QFileInfo(path).fileName();
        setWindowTitle(name+(modified?" *":"")+" — MdEdit");
    }
    void render(){int scroll=preview->verticalScrollBar()->value();preview->document()->setBaseUrl(QUrl::fromLocalFile((path.isEmpty()?QDir::currentPath():QFileInfo(path).absolutePath())+"/"));QStringList errors;preview->setSource(editor->toPlainText());preview->verticalScrollBar()->setValue(scroll);previewAction->setToolTip(errors.isEmpty()?QString::fromUtf8(shortcutLabel)+"E 切换 Live / 源码":"有公式无法解析，已保留源码："+errors.join("; "));}
    bool save(){
        if(path.isEmpty()||!editor->document()->isModified())return true;
        QSaveFile file(path);
        const auto bytes=editor->toPlainText().toUtf8();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
            statusBar()->showMessage("自动保存失败："+file.errorString());
            statusBar()->show();
            return false;
        }
        editor->document()->setModified(false);
        statusBar()->hide();
        return true;
    }
    void load(const QString &file){
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
        editor->moveCursor(QTextCursor::Start);
        preview->moveCursor(QTextCursor::Start);
        pages->currentWidget()->setFocus();
        editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->minimum());
        preview->verticalScrollBar()->setValue(preview->verticalScrollBar()->minimum());
        updateTitle();
    }
protected:
    void closeEvent(QCloseEvent *e)override{
        if(save())e->accept();
        else{
            e->ignore();
            QMessageBox::warning(this,"无法关闭文档",statusBar()->currentMessage());
        }
    }
};

int main(int argc,char **argv){
    #ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc,argv);app.setStyle("Fusion");
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    try{tex::LaTeX::init(formulaResourcePath().toStdString());}catch(const std::exception &e){QMessageBox::critical(nullptr,"公式引擎初始化失败",QString::fromUtf8(e.what()));return 3;}
    int result=0;
    if(app.arguments().contains("--smoke-test")){
        QTextBrowser view;view.resize(900,700);view.setFont(QFont(previewFont,12));QStringList errors;
        int n=renderMarkdown(view.document(),QString::fromUtf8(R"(# Test
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
        for(const QString &nested:{QString("- item\n  ```cpp\n  $code$\nOutside $math$"),
                QString("  - item\n    ```cpp\n    $code$\n    ```\nOutside $math$")}){
            const auto protectedSpans=scanSpans(nested);
            images=images&&protectedSpans.size()==2&&protectedSpans.first().fenced&&
                protectedSpans.last().kind==SpanKind::InlineMath&&
                nested.mid(protectedSpans.last().start,protectedSpans.last().length)=="$math$";
        }
        QTextDocument doc;doc.setPlainText(sample);Highlighter highlighter(&doc);highlighter.rehighlight();
        auto colorAt=[&](int block,int offset){for(const auto &range:doc.findBlockByNumber(block).layout()->formats())if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();return QColor();};
        images=images&&colorAt(1,0)==QColor("#14616b")&&colorAt(2,0)==QColor("#14616b")&&colorAt(3,0)==QColor("#14616b");
        images=images&&!colorAt(4,0).isValid()&&colorAt(7,0)==QColor("#276746");
        {
            QTextDocument codeDocument;
            codeDocument.setPlainText("```cpp\nint value = 42; /* comment\nreturn 1; */\n```\nAfter");
            Highlighter syntax(&codeDocument);syntax.rehighlight();
            auto tokenColor=[&](int row,int offset){
                for(const auto &range:codeDocument.findBlockByNumber(row).layout()->formats())
                    if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();
                return QColor();
            };
            images=images&&tokenColor(1,0)==QColor("#8959a8")&&tokenColor(1,12)==QColor("#c76b29")&&
                tokenColor(2,0)==QColor("#8e908c")&&!tokenColor(4,0).isValid();
        }
        QTextBrowser isolation;isolation.setFont(view.font());isolation.resize(900,700);QStringList isolationErrors;
        images=images&&renderMarkdown(isolation.document(),QString::fromUtf8("前文 $$\\frac{a}{b}$$ 后文 $x^2$ 结束"),isolationErrors)==2&&isolationErrors.isEmpty();
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
        images=checkLive(live.windowTitle()=="MdEdit"&&edit->toPlainText().isEmpty()&&
            edit->placeholderText().isEmpty()&&output->toPlainText().isEmpty()&&!edit->document()->isModified(),__LINE__)&&images;
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
        edit->setPlainText("First _italic_\nSecond _italic_\nThird");settle();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));output->setTextCursor(caret);
        images=checkLive(output->toPlainText()=="First italic\nSecond _italic_\nThird",__LINE__)&&images;
        const QString adjacent="# Markdown 综合测试\n用 MdEdit 打开本文件，按 **Ctrl+E** 切换编辑和预览。\n## ## 1. 中文与基本文字";
        edit->setPlainText(adjacent);settle();
        const QStringList rawLines=adjacent.split('\n');
        const QStringList previewLines={"Markdown 综合测试","用 MdEdit 打开本文件，按 Ctrl+E 切换编辑和预览。","## 1. 中文与基本文字"};
        const QStringList targets={"Markdown","Ctrl+E","中文与基本文字"};
        for(int i=0;i<targets.size();++i){
            caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf(targets[i]));output->setTextCursor(caret);
            QStringList expected=previewLines;
            expected[i]=rawLines[i];
            images=checkLive(output->toPlainText()==expected.join('\n')&&edit->toPlainText()==adjacent,__LINE__)&&images;
        }
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
        edit->setPlainText("# Heading\n> First\n> Second\n\n\n- Third\n  Fourth\n- Fifth\n");settle();
        images=checkLive(output->toPlainText()=="Heading\nFirst\nSecond\n\n\nThird\nFourth\nFifth\n",__LINE__)&&images;
        const auto quoteBlock=output->document()->findBlock(output->toPlainText().indexOf("First"));
        images=checkLive(quoteBlock.blockFormat().background().color()==quoteBackground,__LINE__)&&images;
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
        const QString numbered="Before\n\n1. 1\n2. 2\n3. 3\n4. 4\n\nAfter";
        edit->setPlainText(numbered);settle();
        images=checkLive(output->document()->blockCount()==8,__LINE__)&&images;
        output->setTextCursor(QTextCursor(output->document()->findBlockByNumber(1)));
        for(int line=3;line<=6;++line){
            QApplication::sendEvent(output,&down);
            images=checkLive(liveOutput->lineNumberAt(output->textCursor())==line&&
                (line!=3||output->textCursor().positionInBlock()==0),__LINE__)&&images;
        }
        for(int line=5;line>=2;--line){
            QApplication::sendEvent(output,&up);
            images=checkLive(liveOutput->lineNumberAt(output->textCursor())==line,__LINE__)&&images;
        }
        images=checkLive(edit->toPlainText()==numbered,__LINE__)&&images;
        caret=output->textCursor();
        caret.setPosition(output->document()->findBlockByNumber(3).position());output->setTextCursor(caret);
        const auto nextItem=output->document()->findBlockByNumber(4);
        images=checkLive(output->toPlainText()=="Before\n\n1\n2\n3\n4\n\nAfter"&&
            nextItem.textList()&&nextItem.textList()->itemText(nextItem)=="3.",__LINE__)&&images;
        edit->setPlainText("- First\n  - Nested **bold** $\\frac{a}{b}$\n- Last");settle();
        const auto listItemY=[&](int index){
            const auto block=output->document()->findBlockByNumber(index);
            return output->document()->documentLayout()->blockBoundingRect(block).top();
        };
        const qreal nestedY=listItemY(1), lastY=listItemY(2);
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Nested"));output->setTextCursor(caret);
        images=checkLive(output->toPlainText()=="First\nNested bold "+QString(QChar::ObjectReplacementCharacter)+"\nLast"&&
            output->document()->begin().textList()&&output->document()->lastBlock().textList()&&
            qAbs(listItemY(1)-nestedY)<1&&qAbs(listItemY(2)-lastY)<1,__LINE__)&&images;
        const QString tightList="## Above\n1. First\n2. Second\n## Below";
        edit->setPlainText(tightList);settle();
        const QStringList renderedListLines={"Above","First","Second","Below"};
        const auto rawListLines=tightList.split('\n');
        for(int i=0;i<renderedListLines.size();++i){
            caret=output->textCursor();
            caret.setPosition(output->toPlainText().indexOf(renderedListLines[i]));output->setTextCursor(caret);
            auto expected=renderedListLines;
            if(i==0||i==3)expected[i]=rawListLines[i];
            images=checkLive(output->toPlainText()==expected.join('\n')&&edit->toPlainText()==tightList,__LINE__)&&images;
        }
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
            const auto expected=headingCharacters(level,output->font());
            const auto previewCharacters=output->document()->begin().begin().fragment().charFormat();
            images=checkLive(previewCharacters.fontPointSize()==expected.fontPointSize()&&
                previewCharacters.fontPointSize()>output->font().pointSizeF(),__LINE__)&&images;
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            for(int pos:{0,level+1}){
                QTextCursor sample(output->document());sample.setPosition(pos);sample.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                auto actual=sample.charFormat();
                images=checkLive(actual.fontWeight()==expected.fontWeight()&&
                    actual.fontPointSize()==expected.fontPointSize(),__LINE__)&&images;
            }
            images=checkLive(edit->toPlainText()==heading+"\n\nBody",__LINE__)&&images;
        }
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        // Opening one inline span must leave its siblings rendered.
        edit->setPlainText("Start $x$ and **bold**、*italic*、**second**、`code`\nplus $y$ end");settle();
        const auto formulaObject=QChar::ObjectReplacementCharacter;
        auto visible=output->toPlainText();
        caret=output->textCursor();caret.setPosition(visible.indexOf(formulaObject));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("$x$")&&!visible.contains("**")&&visible.count(formulaObject)==1,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(visible.indexOf("$x$")+1);
        caret.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
        output->setTextCursor(caret);output->insertPlainText("z");
        images=checkLive(edit->toPlainText()=="Start $z$ and **bold**、*italic*、**second**、`code`\nplus $y$ end",__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("bold"));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("**bold**")&&!visible.contains("**second**")&&visible.count(formulaObject)==2,__LINE__)&&images;
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
        // Deleting a paragraph boundary must leave unrelated blocks rendered.
        const QString tail="\n\n**Bold** $x^2$";
        for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
            edit->setPlainText("First\n\nSecond"+tail);settle();
            caret=output->textCursor();
            caret.setPosition(key==Qt::Key_Backspace?output->toPlainText().indexOf("Second"):5);
            output->setTextCursor(caret);
            QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(output,&remove);
            images=checkLive(edit->toPlainText()=="First\nSecond"+tail&&
                !output->toPlainText().contains("**Bold**")&&output->toPlainText().contains(formulaObject),__LINE__)&&images;
        }
        edit->document()->setModified(false);
        // Exercise startup, opening a file, and saving through the editor.
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
        live.show();
        auto *openButton=live.findChild<QPushButton *>("openDocumentButton");
        auto *toolbar=live.findChild<QToolBar *>();
        images=checkLive(openButton->isVisible()&&!toolbar->isVisible()&&!output->isVisible(),__LINE__)&&images;
        openButton->click();
        auto *fileDialog=live.findChild<QFileDialog *>();
        images=checkLive(fileDialog&&fileDialog->isVisible(),__LINE__)&&images;
        if(fileDialog){
            fileDialog->selectFile(filename);
            QMetaObject::invokeMethod(fileDialog,"accept",Qt::DirectConnection);
            settle();
            images=checkLive(!openButton->isVisible()&&toolbar->isVisible()&&toolbar->actions().last()==toggle&&
                output->isVisible()&&!edit->document()->isModified(),__LINE__)&&images;
            images=checkLive(edit->toPlainText()==QString::fromUtf8(contents)&&
                liveOutput->markdown()==edit->toPlainText()&&
                !output->toPlainText().isEmpty(),__LINE__)&&images;

            auto savedText=[&]{QFile file(filename);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
            QTextCursor start(output->document());
            start.setPosition(2);
            output->setTextCursor(start);
            output->insertPlainText("Autosaved");
            QEventLoop waitForSave;
            QTimer::singleShot(850,&waitForSave,&QEventLoop::quit);
            waitForSave.exec();
            images=checkLive(savedText()==edit->toPlainText().toUtf8()&&savedText().contains("Autosaved")&&
                !edit->document()->isModified(),__LINE__)&&images;
            toggle->setChecked(false);
            edit->moveCursor(QTextCursor::End);
            edit->insertPlainText(" on close");
            images=checkLive(live.close()&&savedText()==edit->toPlainText().toUtf8()&&savedText().endsWith(" on close"),__LINE__)&&images;
        }
        {
            QFile longFile(files.filePath("long.md"));
            const QByteArray longText=QByteArray("Paragraph\n\n").repeated(80);
            const bool written=longFile.open(QIODevice::WriteOnly)&&longFile.write(longText)==longText.size();
            longFile.close();
            images=checkLive(written,__LINE__)&&images;
            if(written){
                Window reopening;
                auto *preview=reopening.findChild<QTextBrowser *>("markdownPreview");
                reopening.load(longFile.fileName());
                reopening.show();
                settle();
                images=checkLive(preview->textCursor().atStart()&&preview->verticalScrollBar()->value()==0,__LINE__)&&images;
                preview->moveCursor(QTextCursor::End);
                images=checkLive(preview->verticalScrollBar()->value()>0,__LINE__)&&images;
                reopening.load(longFile.fileName());
                settle();
                images=checkLive(preview->textCursor().atStart()&&preview->verticalScrollBar()->value()==0,__LINE__)&&images;
            }
        }
        {
            LiveEditor focusProbe;
            focusProbe.resize(640,240);
            focusProbe.show();
            focusProbe.setSource(QString("Paragraph\n\n").repeated(60)+"Last paragraph");
            QTextCursor selection(focusProbe.document()->lastBlock());
            focusProbe.setTextCursor(selection);
            selection.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,4);
            focusProbe.setTextCursor(selection);
            focusProbe.ensureCursorVisible();
            const int scroll=focusProbe.verticalScrollBar()->value();
            QFocusEvent focusOut(QEvent::FocusOut,Qt::ActiveWindowFocusReason);
            QFocusEvent focusIn(QEvent::FocusIn,Qt::ActiveWindowFocusReason);
            QApplication::sendEvent(&focusProbe,&focusOut);
            QApplication::sendEvent(&focusProbe,&focusIn);
            images=checkLive(scroll>0&&focusProbe.verticalScrollBar()->value()==scroll&&
                focusProbe.textCursor().position()==selection.position()&&
                focusProbe.textCursor().anchor()==selection.anchor(),__LINE__)&&images;
        }
        // Measure visible layout for units after the first one, where Qt
        // may discard the first block format when inserting a fragment.
        {
            LiveEditor layoutProbe;
            layoutProbe.setFont(output->font());
            layoutProbe.resize(640,480);
            layoutProbe.show();
            QTemporaryFile inlineImageFile;
            QImage tallImage(30,120,QImage::Format_RGB32);
            tallImage.fill(Qt::red);
            const bool inlineImageReady=inlineImageFile.open()&&tallImage.save(&inlineImageFile,"PNG")&&inlineImageFile.flush();
            images=checkLive(inlineImageReady,__LINE__)&&images;
            const QString imageMarkup="Text ![image]("+QUrl::fromLocalFile(inlineImageFile.fileName()).toString(QUrl::FullyEncoded)+") after";
            for(const QString &middle:{QString("Body"),QString("# Heading"),QString("## `code`"),QString("First row\nSecond row"),
                    QString("Body **bold**"),QString("`code`"),QString("**bold**"),QString("_italic_"),
                    QString("`code` _italic_"),QString("~~strike~~ and `code`"),imageMarkup,QString(" \t")}){
                const QString original="Before\n\n"+middle+"\n\nAfter";
                layoutProbe.setSource(original);
                auto *doc=layoutProbe.document();
                auto top=[&](const QTextBlock &block){return doc->documentLayout()->blockBoundingRect(block).top();};
                const qreal before=top(doc->lastBlock());
                const auto unit=doc->findBlockByNumber(2);
                const qreal unitBefore=top(unit);
                const auto baseline=[&]{
                    const auto block=doc->findBlockByNumber(2);
                    doc->documentLayout()->blockBoundingRect(block);
                    const auto line=block.layout()->lineAt(0);
                    return line.y()+line.ascent();
                };
                const qreal baselineBefore=baseline();
                if(middle==imageMarkup){
                    QTextCursor image(unit);
                    image.setPosition(unit.position()+unit.text().indexOf(formulaObject));
                    image.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                    const auto format=image.charFormat().toImageFormat();
                    images=checkLive(format.isValid()&&format.height()>0&&format.height()<tallImage.height()&&
                        qAbs(format.height()-format.width()*4)<0.01,__LINE__)&&images;
                }
                layoutProbe.setTextCursor(QTextCursor(unit));
                images=checkLive(before>unitBefore&&unitBefore>0&&qAbs(top(doc->lastBlock())-before)<1.0&&
                    qAbs(top(doc->findBlockByNumber(2))-unitBefore)<1.0&&layoutProbe.markdown()==original,__LINE__)&&images;
                images=checkLive(qAbs(baseline()-baselineBefore)<1.0,__LINE__)&&images;
            }
            auto fenceAlpha=[&](int position){
                QTextCursor fence(layoutProbe.document());fence.setPosition(position);
                fence.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                return fence.charFormat().foreground().color().alpha();
            };
            // Fence rows stay in place; only their text disappears in preview.
            for(const auto &entry:QList<QPair<QString,QString>>{{"cpp","int value = 42; // $literal$"},
                    {"python","def square(x):\n    return \"$literal$\""},{"latex","\\frac{1}{2} % $literal$"},
                    {"json","{\"value\": true, \"count\": 42}"},{"javascript","const value = \"$literal$\";"}}){
                const QString raw="Before\n\n```"+entry.first+"\n"+entry.second+"\n```\nAfter";
                layoutProbe.setSource(raw);
                const QString previewText=layoutProbe.toPlainText();
                const int bodyStart=raw.indexOf(entry.second);
                const int visibleStart=previewText.indexOf(entry.second);
                QSet<QRgb> colors;
                for(auto block=layoutProbe.document()->findBlock(visibleStart);block.isValid()&&block.position()<visibleStart+entry.second.size();block=block.next())
                    for(auto it=block.begin();!it.atEnd();++it)colors.insert(it.fragment().charFormat().foreground().color().rgb());
                const int opening=raw.indexOf("```"),closing=raw.lastIndexOf("```");
                const auto bodyTop=layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->findBlock(visibleStart)).top();
                const auto tailTop=layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top();
                images=checkLive(previewText==raw&&fenceAlpha(opening)==0&&fenceAlpha(closing)==0&&visibleStart>=0&&colors.size()>1&&
                    !previewText.contains(QChar::ObjectReplacementCharacter)&&layoutProbe.markdown()==raw,__LINE__)&&images;
                QTextCursor click(layoutProbe.document());click.setPosition(visibleStart+2);layoutProbe.setTextCursor(click);
                images=checkLive(layoutProbe.sourceSelection()==qMakePair(bodyStart+2,bodyStart+2)&&
                    layoutProbe.toPlainText()==raw&&fenceAlpha(opening)==255&&fenceAlpha(closing)==255&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->findBlock(visibleStart)).top()==bodyTop&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top()==tailTop,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(layoutProbe.document()->lastBlock()));
                images=checkLive(layoutProbe.toPlainText()==previewText&&layoutProbe.markdown()==raw&&
                    fenceAlpha(opening)==0&&fenceAlpha(closing)==0&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top()==tailTop,__LINE__)&&images;
            }
            for(const QString &raw:{QString("```cpp\nint value = 42;"),QString("~~~python\nprint('x')\n~~~"),
                    QString("```text\n$literal$\n```"),QString("```cpp\n```"),QString("```cpp")}){
                layoutProbe.setSource(raw+"\n");
                images=checkLive(fenceAlpha(0)==0&&layoutProbe.toPlainText()==raw+"\n"&&
                    layoutProbe.markdown()==raw+"\n",__LINE__)&&images;
            }
            layoutProbe.resize(340,480);
            const QString longCode=QString("some code with spaces ").repeated(4)+"$x$";
            for(const QString &code:{"```cpp\n"+longCode+"\n\n  next\n```",QString("~~~\n\t$x$\n~~~"),
                    QString("    indented\n    code"),QString("    $x$\n\n    **code**"),
                    QString("> ```\n> > literal $x$\n> ```")}){
                const QString original="Before\n\n"+code+"\nAfter";
                layoutProbe.setSource(original);
                auto *doc=layoutProbe.document();
                auto geometry=[&]{
                    QList<QRectF> rects;
                    for(auto block=doc->begin();block.isValid();block=block.next())
                        rects.append(doc->documentLayout()->blockBoundingRect(block));
                    return rects;
                };
                const auto resting=geometry();
                layoutProbe.setTextCursor(QTextCursor(doc->findBlockByNumber(3)));
                images=checkLive(geometry()==resting&&layoutProbe.toPlainText()==original&&
                    layoutProbe.lineNumberAt(layoutProbe.textCursor())==4,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(doc->lastBlock()));
                images=checkLive(geometry()==resting&&layoutProbe.markdown()==original,__LINE__)&&images;
            }
            layoutProbe.resize(640,480);
            const QString editableCode="Before\n\n```cpp\n  value = $x$;\n\n```\nAfter";
            layoutProbe.setSource(editableCode);
            auto *codeDocument=layoutProbe.document();
            QTextCursor codeCaret(codeDocument->findBlockByNumber(3));
            layoutProbe.setTextCursor(codeCaret);
            const auto codeFormat=layoutProbe.textCursor().blockFormat();
            const auto codeFont=layoutProbe.textCursor().block().begin().fragment().charFormat().font();
            QKeyEvent codeEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
            QApplication::sendEvent(&layoutProbe,&codeEnter);
            QKeyEvent codeTyping(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
            QApplication::sendEvent(&layoutProbe,&codeTyping);
            const QString editedCode="Before\n\n```cpp\n\nx  value = $x$;\n\n```\nAfter";
            images=checkLive(layoutProbe.markdown()==editedCode&&
                layoutProbe.textCursor().blockFormat()==codeFormat&&
                layoutProbe.textCursor().charFormat().font()==codeFont,__LINE__)&&images;
            const qreal tailBefore=codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top();
            layoutProbe.setTextCursor(QTextCursor(codeDocument->lastBlock()));
            images=checkLive(qAbs(codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top()-tailBefore)<1.0&&
                layoutProbe.markdown()==editedCode&&fenceAlpha(editedCode.indexOf("```"))==0,__LINE__)&&images;
            for(const QString &code:{QString("- item\n  ```cpp\n  value\n  ```"),
                    QString("  - item\n    ```cpp\n    value\n    ```")}){
                const QString original="Before\n\n"+code+"\n\nAfter";
                layoutProbe.setSource(original);
                auto tailTop=[&]{return codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top();};
                const qreal before=tailTop();
                QTextCursor value(codeDocument);
                value.setPosition(layoutProbe.toPlainText().indexOf("value"));
                layoutProbe.setTextCursor(value);
                images=checkLive(qAbs(tailTop()-before)<1.0&&layoutProbe.markdown()==original,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(codeDocument->lastBlock()));
                images=checkLive(qAbs(tailTop()-before)<1.0,__LINE__)&&images;
            }
        }
        {
            LiveEditor probe;
            probe.resize(640,480);
            probe.show();
            const QString linkSource="Before\n\n[Browser link](https://example.com#section)\n\n[Section](#section)";
            probe.setSource(linkSource);
            QList<QUrl> clickedLinks;
            QObject::connect(&probe,&QTextBrowser::anchorClicked,&probe,[&](const QUrl &url){clickedLinks.append(url);});
            for(const QString &label:{QString("Browser link"),QString("Section")}){
                QTextCursor link(probe.document());
                link.setPosition(probe.toPlainText().indexOf(label)+2);
                const QPoint point=probe.cursorRect(link).center();
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&press);
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&release);
            }
            images=checkLive(clickedLinks==QList<QUrl>{QUrl("https://example.com#section"),QUrl("#section")}&&
                probe.markdown()==linkSource&&!probe.toPlainText().contains("](https:"),__LINE__)&&images;
            probe.setSource("Plain text\n\nBefore $\\frac{a}{b}$ after\n\nTail");
            const auto lineHeight=[&](int blockNumber){
                const auto block=probe.document()->findBlockByNumber(blockNumber);
                const auto *layout=probe.document()->documentLayout();
                return layout->blockBoundingRect(block.next()).top()-layout->blockBoundingRect(block).top();
            };
            const qreal plainHeight=lineHeight(0);
            const auto formulaBlock=probe.document()->findBlock(probe.toPlainText().indexOf(formulaObject));
            const int formulaBlockNumber=formulaBlock.blockNumber();
            const auto baseline=[&]{
                const auto block=probe.document()->findBlockByNumber(formulaBlockNumber);
                const auto rect=probe.document()->documentLayout()->blockBoundingRect(block);
                const auto line=block.layout()->lineAt(0);
                return rect.top()+line.y()+line.ascent();
            };
            const qreal renderedBaseline=baseline();
            QTextCursor formulaCursor(probe.document());
            formulaCursor.setPosition(probe.toPlainText().indexOf(formulaObject));
            formulaCursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            const auto inlineImage=formulaCursor.charFormat().toImageFormat();
            const qreal paintAscent=inlineImage.doubleProperty(formulaPaintAscent);
            const qreal paintDescent=inlineImage.doubleProperty(formulaPaintHeight)-paintAscent;
            images=checkLive(inlineImage.isValid()&&paintAscent>0&&paintDescent>0&&
                qAbs(inlineImage.height()-paintAscent)<0.01&&
                paintDescent<=QFontMetricsF(probe.document()->defaultFont()).descent()+0.01,__LINE__)&&images;
            images=checkLive(qAbs(lineHeight(formulaBlockNumber)-plainHeight)<1.0,__LINE__)&&images;
            caret=probe.textCursor();caret.setPosition(probe.toPlainText().indexOf(formulaObject));
            probe.setTextCursor(caret);
            images=checkLive(probe.toPlainText().contains("$\\frac{a}{b}$")&&
                qAbs(lineHeight(formulaBlockNumber)-plainHeight)<1.0&&
                qAbs(baseline()-renderedBaseline)<1.0,__LINE__)&&images;
            probe.setSource("Start $x$ and **bold**、*italic*、**second**、`code`\nplus $y$ end");
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
            const QString tail=QString("\n\nTail").repeated(24);
            probe.setSource("**one** and **two** remainder"+tail);
            QTextCursor split=probe.textCursor();
            split.setPosition(probe.toPlainText().indexOf("remainder"));probe.setTextCursor(split);
            probe.verticalScrollBar()->setValue(0);
            const int beforeEnter=probe.cursorRect().top();
            QKeyEvent splitLine(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
            QApplication::sendEvent(&probe,&splitLine);
            images=checkLive(probe.markdown()=="**one** and **two** \nremainder"+tail&&
                probe.cursorRect().top()>beforeEnter&&probe.lineNumberAt(probe.textCursor())==2,__LINE__)&&images;
        }
        {
            LiveEditor probe;
            probe.resize(640,360);probe.show();
            const QString original="- First **bold** $x$\n  - Nested\n- Last";
            for(int line:{0,1,2}){
                probe.setSource(original);
                const auto block=probe.document()->findBlockByNumber(line);
                const QRect bodyEdge=probe.cursorRect(QTextCursor(block));
                const QPoint point(bodyEdge.left()-12,bodyEdge.center().y());
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&press);
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&release);
                const int lineStart=line==0?0:original.indexOf(line==1?"  - Nested":"- Last");
                const int marker=lineStart+(line==1?2:0);
                images=checkLive(probe.sourceSelection()==qMakePair(marker,marker)&&
                    probe.textCursor().block().text().startsWith(line==1?"  - ":"- ")&&
                    !probe.textCursor().block().textList()&&!probe.toPlainText().contains("**")&&
                    probe.toPlainText().count(QChar::ObjectReplacementCharacter)==1,__LINE__)&&images;
                QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(&probe,&remove);
                QString expected=original;expected.remove(marker,1);
                images=checkLive(probe.markdown()==expected,__LINE__)&&images;
            }
            probe.setSource(original);probe.restoreSourceSelection(2,2);
            QKeyEvent left(QEvent::KeyPress,Qt::Key_Left,Qt::NoModifier);
            QApplication::sendEvent(&probe,&left);
            images=checkLive(probe.sourceSelection()==qMakePair(1,1)&&
                probe.toPlainText().startsWith("- First bold "),__LINE__)&&images;
        }
        {
            LiveEditor probe;
            const QString list="- First\n  - Nested **bold** $x$ and $y$\n- Last";
            const QChar object=QChar::ObjectReplacementCharacter;
            probe.setSource(list);
            probe.restoreSourceSelection(list.indexOf("Nested"),list.indexOf("Nested"));
            images=checkLive(probe.toPlainText()=="First\nNested bold "+QString(object)+" and "+QString(object)+"\nLast"&&
                probe.textCursor().block().textList()&&probe.document()->indentWidth()==24,__LINE__)&&images;
            const int bold=list.indexOf("**bold**");
            probe.restoreSourceSelection(bold,bold);
            images=checkLive(probe.toPlainText().contains("**bold**")&&
                probe.toPlainText().count(object)==2&&probe.textCursor().block().textList(),__LINE__)&&images;
            const int formula=list.indexOf("$x$");
            probe.restoreSourceSelection(formula+1,formula+1);
            images=checkLive(probe.toPlainText().contains("$x$")&&!probe.toPlainText().contains("**")&&
                probe.toPlainText().count(object)==1&&probe.sourceSelection()==qMakePair(formula+1,formula+1),__LINE__)&&images;
            QKeyEvent typeZ(QEvent::KeyPress,Qt::Key_Z,Qt::NoModifier,"z");
            QApplication::sendEvent(&probe,&typeZ);
            QString editedList=list;editedList.insert(formula+1,'z');
            images=checkLive(probe.markdown()==editedList&&probe.textCursor().block().textList(),__LINE__)&&images;
            probe.restoreSourceSelection(2,2);
            QKeyEvent removeSpace(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
            QApplication::sendEvent(&probe,&removeSpace);
            images=checkLive(probe.markdown()==QString("-First")+editedList.mid(7),__LINE__)&&images;
        }
        {
            LiveEditor probe;
            const QList<QPair<QString,QString>> cases={
                {"- first","- first\n- "},{"9) item","9) item\n10) "},
                {"  - [x] done","  - [x] done\n  - [ ] "},
                {"- ",""},{"intro\n\n- ","intro\n\n"},
                {"```\n- code\n```","```\n- code\n\n```"},
                {"$$\n- math\n$$","$$\n- math\n\n$$"}};
            for(const auto &entry:cases){
                probe.setSource(entry.first);
                const int at=entry.first.contains("code")?entry.first.indexOf("code")+4:
                    entry.first.contains("math")?entry.first.indexOf("math")+4:entry.first.size();
                probe.restoreSourceSelection(at,at);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
                QApplication::sendEvent(&probe,&enter);
                images=checkLive(probe.markdown()==entry.second,__LINE__)&&images;
            }
            probe.setSource("- first");probe.restoreSourceSelection(7,7);
            QKeyEvent softBreak(QEvent::KeyPress,Qt::Key_Return,Qt::ShiftModifier,"\r");
            QApplication::sendEvent(&probe,&softBreak);
            images=checkLive(probe.markdown()=="- first\n",__LINE__)&&images;
            edit->setPlainText("# Heading\n\nSome **bold** text");settle();
            toggle->setChecked(false);
            QTextCursor selection(edit->document());selection.setPosition(24);selection.setPosition(16,QTextCursor::KeepAnchor);
            edit->setTextCursor(selection);toggle->setChecked(true);toggle->setChecked(false);
            images=checkLive(edit->textCursor().anchor()==24&&edit->textCursor().position()==16,__LINE__)&&images;
            toggle->setChecked(true);liveOutput->restoreSourceSelection(5,5);toggle->setChecked(false);
            images=checkLive(edit->textCursor().position()==5&&!edit->textCursor().hasSelection(),__LINE__)&&images;
        }
        {
            LiveEditor probe;probe.resize(640,360);probe.show();
            QString text;
            for(int i=0;i<80;++i)text+=QString("Paragraph %1 **bold**\n\n").arg(i);
            for(bool backspace:{true,false}){
                probe.setSource(text);probe.restoreSourceSelection(text.indexOf("Paragraph 35"),text.indexOf("Paragraph 35"));
                const int begin=text.indexOf("Paragraph 35");
                // Select a whole source line and its following blank line.
                const auto visible=probe.toPlainText();
                QTextCursor selected(probe.document());selected.setPosition(visible.indexOf("Paragraph 35"));
                selected.setPosition(visible.indexOf("Paragraph 36"),QTextCursor::KeepAnchor);probe.setTextCursor(selected);
                probe.verticalScrollBar()->setValue(probe.verticalScrollBar()->value()+probe.cursorRect().top()-160);
                const int scroll=probe.verticalScrollBar()->value();
                QKeyEvent remove(QEvent::KeyPress,backspace?Qt::Key_Backspace:Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(&probe,&remove);
                QString expected=text;expected.remove(begin,text.indexOf("Paragraph 36")-begin);
                images=checkLive(probe.markdown()==expected&&probe.verticalScrollBar()->value()==scroll,__LINE__)&&images;
            }
        }
        {
            LiveEditor codePreview;codePreview.setFont(QFont(previewFont,12));codePreview.resize(760,560);
            codePreview.document()->setDocumentMargin(24);
            codePreview.setSource("# Code preview\n\n```cpp\n#include <iostream>\nint main() {\n    // $x$ stays literal\n    std::cout << \"Hello, Markdown!\";\n    return 0;\n}\n```\n\n```python\ndef square(x):\n    return x ** 2\n\nprint(square(5))\n```\n\nEnd");
            codePreview.grab().save(QDir::current().absoluteFilePath("code-smoke.png"));
        }
        QImage shot(view.size(),QImage::Format_ARGB32);shot.fill(QColor("#ffffff"));view.setStyleSheet("color:#26232d;background:#ffffff;");view.render(&shot);shot.save(QDir::current().absoluteFilePath("math-smoke.png"));
        if(!images){qWarning()<<n<<errors;result=1;}
    }else{Window w;if(app.arguments().size()>1)w.load(app.arguments().at(1));w.show();result=app.exec();}
    tex::LaTeX::release();return result;
}

#include "main.moc"



