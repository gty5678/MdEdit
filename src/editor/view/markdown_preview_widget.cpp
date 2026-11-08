#include "markdown_preview_widget.h"
#include "../render/renderer_registry.h"
namespace mdedit {
namespace {
class PreviewTextDocument final:public QTextDocument {
protected:
    QVariant loadResource(int,const QUrl &) override {return {};}
};
}

MarkdownPreviewWidget::MarkdownPreviewWidget(QWidget *parent):QAbstractScrollArea(parent),resources(this) {
    setObjectName("resourcePreview");setFrameShape(QFrame::NoFrame);
    connect(&resources,&ResourceManager::ready,this,[this]{relayout();});
    connect(verticalScrollBar(),&QScrollBar::valueChanged,viewport(),qOverload<>(&QWidget::update));
}
void MarkdownPreviewWidget::setSource(const QString &text,const QUrl &baseUrl) {
    if(source==text&&base==baseUrl&&!entries.empty())return;
    source=text;base=baseUrl;entries.clear();
    for(const auto &block:MarkdownParser::parse(source)) {
        Entry entry;entry.block=block;
        if(!block.resource){
            entry.text=std::make_unique<PreviewTextDocument>();
            entry.text->setDefaultFont(font());entry.text->setDocumentMargin(0);
            QString fragment=source.mid(block.source.begin,block.source.end-block.source.begin);
            // The resource pipeline owns external I/O; inline images retain readable alt text.
            fragment.replace(QRegularExpression(R"(!\[([^\]]*)\]\([^)]*\))"),QStringLiteral("[图片：\\1]"));
            entry.text->setMarkdown(fragment,QTextDocument::MarkdownDialectGitHub);
        }
        entries.push_back(std::move(entry));
    }
    relayout();
}
QString MarkdownPreviewWidget::resourceKey(const Entry &entry) {
    return resources.request(ResourceResolver::resolve(entry.block.resource->uri,base));
}
void MarkdownPreviewWidget::relayout() {
    qreal y=24;const qreal width=qMax(80,viewport()->width()-48);
    for(auto &entry:entries){
        entry.top=y;
        if(entry.text){entry.text->setTextWidth(width);entry.height=entry.text->size().height();}
        else {
            const auto &ref=*entry.block.resource;
            if(!ResourceResolver::isPreviewResource(ResourceResolver::resolve(ref.uri,base))){
                entry.height=QFontMetricsF(font()).height()+12;
                y+=entry.height+18;continue;
            }
            entry.height=260;
            const QString suffix=QFileInfo(QUrl(ref.uri).path()).suffix().toLower();
            if(suffix!="pdf"&&suffix!="csv"&&!ResourceResolver::isTextResource(QUrl(ref.uri))){
                const auto *data=resources.lookup(resourceKey(entry));
                if(data&&!data->image.isNull()){
                    const qreal w=ref.width>0?ref.width:qMin(width,qreal(data->image.width()));
                    entry.height=qBound(1.0,w*data->image.height()/data->image.width(),1000000.0);
                }
            }
        }
        y+=entry.height+18;
    }
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0,qMax(0,int(y+24-viewport()->height())));viewport()->update();
}
void MarkdownPreviewWidget::refreshAppearance(){for(auto &entry:entries)if(entry.text)entry.text->setDefaultFont(font());relayout();}
void MarkdownPreviewWidget::refreshResources(){resources.clear();viewport()->update();}
void MarkdownPreviewWidget::resizeEvent(QResizeEvent *event){QAbstractScrollArea::resizeEvent(event);relayout();}
void MarkdownPreviewWidget::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());painter.fillRect(viewport()->rect(),Qt::white);
    const int scroll=verticalScrollBar()->value();
    auto start=std::lower_bound(entries.begin(),entries.end(),scroll,[](const Entry &e,int y){return e.top+e.height<y;});
    for(auto it=start;it!=entries.end()&&it->top<scroll+viewport()->height();++it){
        auto &entry=*it;painter.save();painter.translate(24,entry.top-scroll);
        QRectF box(0,0,qMax(80,viewport()->width()-48),entry.height);painter.setClipRect(box);
        if(entry.text)entry.text->drawContents(&painter,box);
        else {
            painter.fillRect(box,QColor("#f5f2f8"));painter.setPen(QColor("#524960"));
            const auto &ref=*entry.block.resource;
            const QUrl file=ResourceResolver::resolve(ref.uri,base);
            if(!ResourceResolver::isPreviewResource(file)){
                painter.setPen(QColor("#655080"));
                painter.drawText(box.adjusted(10,0,-10,0),Qt::AlignVCenter|Qt::TextSingleLine,
                    painter.fontMetrics().elidedText(QStringLiteral("📄  ")+QFileInfo(file.path()).fileName(),Qt::ElideMiddle,int(box.width()-20)));
                painter.restore();continue;
            }
            const QString suffix=QFileInfo(QUrl(ref.uri).path()).suffix().toLower();
            const bool csv=suffix=="csv",picture=!csv&&suffix!="pdf"&&!ResourceResolver::isTextResource(QUrl(ref.uri));
            painter.save();
            if(csv){auto titleFont=painter.font();titleFont.setBold(true);painter.setFont(titleFont);}
            const QString title=csv?(ref.alt.trimmed().isEmpty()?QStringLiteral("CSV 表格"):ref.alt):ref.alt+" · "+ref.uri;
            if(!picture)painter.drawText(box.adjusted(12,8,-12,0),Qt::AlignTop|Qt::TextSingleLine,
                painter.fontMetrics().elidedText(title,Qt::ElideRight,qMax(1,int(box.width()-24))));
            painter.restore();
            const auto *data=resources.lookup(resourceKey(entry));
            QRectF content=box.adjusted(12,36,-12,-12);
            if(picture){
                const qreal width=ref.width>0?ref.width:data&&!data->image.isNull()?qMin(box.width(),qreal(data->image.width())):box.width();
                content=QRectF(0,0,width,entry.height);
            }
            if(!data)painter.drawText(content,Qt::AlignCenter,"正在加载…");
            else RendererRegistry::paint(painter,*data,content);
        }
        painter.restore();
    }
}
void MarkdownPreviewWidget::mousePressEvent(QMouseEvent *event){
    const qreal y=event->position().y()+verticalScrollBar()->value();
    for(const auto &entry:entries)if(y>=entry.top&&y<entry.top+entry.height){
        if(entry.block.resource){
            const QUrl file=ResourceResolver::resolve(entry.block.resource->uri,base);
            if(!ResourceResolver::isPreviewResource(file)&&file.isLocalFile()&&event->button()==Qt::LeftButton){QDesktopServices::openUrl(file);event->accept();return;}
        }
        emit sourceActivated(entry.block.source.begin);break;
    }
    QAbstractScrollArea::mousePressEvent(event);
}
}
