#include "live_editor.h"
#include "../markdown/scanner.h"
#include "../markdown/renderer.h"
#include "markdown/block_parser.h"
#include "render/renderer_registry.h"
#include "tables/table_editor_dialog.h"
#include <limits>
#include <QPdfView>
#include <QPdfDocument>
#include <QPdfPageNavigator>

namespace {
class NestedPdfView final : public QPdfView {
    QPointer<QAbstractScrollArea> outerArea;
protected:
    bool eventFilter(QObject *watched,QEvent *event) override {
        if(watched==verticalScrollBar()&&event->type()==QEvent::Wheel){wheelEvent(static_cast<QWheelEvent*>(event));return true;}
        return QPdfView::eventFilter(watched,event);
    }
    void wheelEvent(QWheelEvent *event) override {
        const int delta=event->pixelDelta().isNull()?event->angleDelta().y():event->pixelDelta().y();
        const auto *bar=verticalScrollBar();
        const bool outward=(delta>0&&bar->value()<=bar->minimum())||(delta<0&&bar->value()>=bar->maximum());
        if(outward&&outerArea&&event->modifiers()==Qt::NoModifier){
            const qreal distance=event->pixelDelta().isNull()
                ?event->angleDelta().y()/120.0*QApplication::wheelScrollLines()*QFontMetricsF(outerArea->font()).height()
                :event->pixelDelta().y();
            auto *outerBar=outerArea->verticalScrollBar();outerBar->setValue(outerBar->value()-qRound(distance));
            event->accept();return;
        }
        if(!event->pixelDelta().isNull()&&event->modifiers()==Qt::NoModifier){
            verticalScrollBar()->setValue(verticalScrollBar()->value()-event->pixelDelta().y());
        }else QPdfView::wheelEvent(event);
        // Never let an unconsumed event bubble to the document accidentally.
        event->accept();
    }
public:
    NestedPdfView(QAbstractScrollArea *outer,QWidget *parent):QPdfView(parent),outerArea(outer){verticalScrollBar()->installEventFilter(this);}
    ~NestedPdfView() override {setDocument(nullptr);}
};
// All external image I/O goes through the same asynchronous cache as PDF/CSV.
class ResourceTextDocument final : public QTextDocument {
    mdedit::ResourceManager &resources;
protected:
    QVariant loadResource(int type,const QUrl &url) override {
        if(type!=QTextDocument::ImageResource)return {};
        const auto *data=resources.lookup(resources.request(baseUrl().resolved(url)));
        return data&&!data->image.isNull()?QVariant(data->image):QVariant();
    }
public:
    explicit ResourceTextDocument(mdedit::ResourceManager &manager):resources(manager){}
};
}

QString LiveEditor::clickableAnchorAt(const QPoint &point) const{
        const int index=regionAt(cursorForPosition(point).position());
        if(index>=0){
            const auto &region=regions[index];
            const auto nodes=mdedit::MarkdownParser::parse(source.mid(region.sourceStart,region.sourceEnd-region.sourceStart));
            if(nodes.size()==1&&nodes.first().resource){
                const QUrl file=mdedit::ResourceResolver::resolve(nodes.first().resource->uri,document()->baseUrl());
                const auto block=document()->findBlock(region.start);
                const auto rect=document()->documentLayout()->blockBoundingRect(block).translated(-horizontalScrollBar()->value(),-verticalScrollBar()->value());
                if(!mdedit::ResourceResolver::isPreviewResource(file)&&!block.text().contains("![")&&rect.contains(point)){
                    QUrl action("mdedit-pdf:/open");QUrlQuery query;
                    query.addQueryItem("file",file.toString());action.setQuery(query);return action.toString();
                }
            }
        }
        const QString anchor=anchorAt(point);
        const QUrl url(anchor);
        if(url.scheme()=="mdedit-pdf")return anchor;
        if((url.isRelative()&&anchor.startsWith('#')&&!url.fragment().isEmpty())||
           ((url.scheme()=="http"||url.scheme()=="https")&&url.isValid()&&!url.host().isEmpty()))
            return anchor;
        return {};
    }

LiveEditor::LiveEditor(){
        copyFeedbackTimer.setSingleShot(true);
        copyFeedbackTimer.setInterval(1500);
        connect(&copyFeedbackTimer,&QTimer::timeout,this,[this]{copiedCodeSource=-1;viewport()->update();});
        axisDragScroll.setInterval(35);
        connect(&axisDragScroll,&QTimer::timeout,this,[this]{
            if(axisPressed&&axisStarted){
                const int delta=axisPoint.y()<32?-18:axisPoint.y()>viewport()->height()-32?18:0;
                const int dx=axisPoint.x()<32?-18:axisPoint.x()>viewport()->width()-32?18:0;
                if(delta||dx){if(axisRow)verticalScrollBar()->setValue(verticalScrollBar()->value()+delta);else horizontalScrollBar()->setValue(horizontalScrollBar()->value()+dx);updateAxisDrop();viewport()->update();}return;
            }
        });
        // Publish a burst of worker results in one layout pass. Keep the
        // viewport anchor and caret; refreshAppearance also exposes the caret
        // and calls ensureCursorVisible, which is wrong during scrolling.
        resourceRefreshTimer.setSingleShot(true);
        resourceRefreshTimer.setInterval(40);
        connect(&resourceManager,&mdedit::ResourceManager::ready,this,[this]{
            if(!resourceRefreshTimer.isActive())resourceRefreshTimer.start();
        });
        connect(&resourceRefreshTimer,&QTimer::timeout,this,[this]{
            for(const auto &key:resourceRenderKeys)renderedMarkdown.remove(key);
            resourceRenderKeys.clear();
            if(sourceExposed)return;
            const auto selection=textCursor();
            const int anchor=sourcePosition(selection.anchor());
            const int position=sourcePosition(selection.position());
            const int chosen=active;
            const int offset=chosen>=0?position-regions[chosen].sourceStart:0;
            QScopedValueRollback<bool> refreshing(backgroundRefresh,true);
            rebuild(chosen,offset);
            const int scroll=verticalScrollBar()->value();
            if(selection.hasSelection()){
                // Restore a selection without scrolling it into view.
                auto visible=[&](int sourceOffset){
                    int nearest=0,distance=int(source.size())+1;
                    for(const auto &region:regions){
                        if(sourceOffset<region.sourceStart||sourceOffset>region.sourceEnd)continue;
                        for(int at=region.start;at<=region.end;++at){
                            const int difference=qAbs(sourcePosition(at)-sourceOffset);
                            if(difference<distance){nearest=at;distance=difference;}
                            if(!difference)return at;
                        }
                    }
                    return nearest;
                };
                QScopedValueRollback<bool> guard(rebuilding,true);
                QTextCursor cursor(document());cursor.setPosition(visible(anchor));
                cursor.setPosition(visible(position),QTextCursor::KeepAnchor);setTextCursor(cursor);
                verticalScrollBar()->setValue(scroll);
            }
        });
        document()->setIndentWidth(24);
        installFormulaImageHandler(document());
        setReadOnly(false);setAcceptRichText(false);setUndoRedoEnabled(false);setAcceptDrops(false);setContextMenuPolicy(Qt::DefaultContextMenu);
        viewport()->setMouseTracking(true);
        connect(this,&QTextEdit::cursorPositionChanged,this,[this]{
            // Cursor signals can arrive before textChanged during a multiline edit.
            // Wait for source/region offsets to catch up before switching units.
            if(rebuilding||movingCursor||textCursor().hasSelection()||toPlainText()!=oldText)return;
            activateCaretRegion();
        });
        connect(this,&QTextEdit::textChanged,this,[this]{
            if(rebuilding)return;
            if(syncNativeTable())return;
            if(active<0)return;
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
            oldText=now;styleActiveSource();preserveResourceSlot();refreshLineNumbers();if(sourceChanged)sourceChanged(source);
        });
    }

void LiveEditor::setSource(const QString &text){if(text==source&&!regions.isEmpty())return;cancelAxisDrag();axisSource=axisIndex=-1;resourceSourceEditStart=-1;source=text;rebuild(-1,0);}

QString LiveEditor::markdown()const{return source;}

void LiveEditor::refreshAppearance(){
        const auto range=sourceSelection();rebuild(-1,0);restoreSourceSelection(range.first,range.second);
    }

QPair<int,int> LiveEditor::sourceSelection()const{
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1)
            return caret.anchor()<caret.position()?qMakePair(0,int(source.size())):qMakePair(int(source.size()),0);
        return {sourcePosition(caret.anchor()),sourcePosition(caret.position())};
    }

void LiveEditor::restoreSourceSelection(int anchor,int position){
        if(resourceSourceEditStart>=0){
            bool inside=false;
            for(const auto &node:mdedit::MarkdownParser::parse(source))
                if(node.resource&&node.source.begin==resourceSourceEditStart&&anchor>=node.source.begin&&anchor<=node.source.end)inside=true;
            if(!inside)resourceSourceEditStart=-1;
        }
        anchor=qBound(0,anchor,int(source.size()));position=qBound(0,position,int(source.size()));
        rebuild(-1,0,anchor==position?position:qMin(anchor,position));
        if(anchor!=position){
            // Restore menu/search selections in the live document. Exposing
            // the complete source is only needed immediately before native
            // edits that cross rendered regions.
            auto visiblePosition=[&](int sourceOffset){
                int nearest=0,distance=int(source.size())+1;
                for(const auto &region:regions){
                    if(sourceOffset<region.sourceStart||sourceOffset>region.sourceEnd)continue;
                    for(int at=region.start;at<=region.end;++at){
                        const int difference=qAbs(sourcePosition(at)-sourceOffset);
                        if(difference<distance){nearest=at;distance=difference;}
                        if(!difference)return at;
                    }
                }
                return nearest;
            };
            QScopedValueRollback<bool> guard(rebuilding,true);
            QTextCursor selected(document());selected.setPosition(visiblePosition(anchor));
            selected.setPosition(visiblePosition(position),QTextCursor::KeepAnchor);
            setTextCursor(selected);
        }
        ensureCursorVisible();
    }

int LiveEditor::sourceLineCount()const {return source.count('\n')+1;}

int LiveEditor::lineNumberAt(const QTextCursor &cursor)const {
        if(const auto table=cursor.currentTable()){
            const int index=regionAt(table->firstPosition());
            if(index>=0){
                const auto &region=regions[index];
                const auto lines=source.mid(region.sourceStart,region.sourceEnd-region.sourceStart).split('\n');
                static const QRegularExpression divider(R"(^\s*\|?\s*:?-+:?\s*(?:\|\s*:?-+:?\s*)+\|?\s*$)");
                for(int i=1;i<lines.size();++i)if(divider.match(lines[i]).hasMatch()){
                    const int row=table->cellAt(cursor).row();
                    return source.left(region.sourceStart).count('\n')+i+row+(row>0?1:0);
                }
            }
        }
        return source.left(sourcePosition(cursor.position())).count('\n')+1;
    }

void LiveEditor::rebuild(int chosen,int offset,int forcedTarget){
        nativeTable.clear();nativeStart=nativeEnd=-1;sourceCellRow=sourceCellColumn=-1;
        rebuilding=true;int scroll=verticalScrollBar()->value();
        // Clearing/repopulating the document temporarily collapses the scroll
        // range. Publish only the final layout and restored viewport together.
        const bool updatesWereEnabled=updatesEnabled();
        setUpdatesEnabled(false);
        const auto restoreUpdates=qScopeGuard([this,updatesWereEnabled]{setUpdatesEnabled(updatesWereEnabled);});
        // Intermediate document mutations must not repeatedly update the gutter.
        const QSignalBlocker blockEditor(this);
        sourceExposed=false;
        const int caretY=cursorRect().top();
        const bool wasEditingResource=isResourceRegion(active);
        int candidate=chosen;
        if(forcedTarget>=0)for(int i=0;i<regions.size();++i)
            if(forcedTarget>=regions[i].sourceStart&&forcedTarget<regions[i].sourceEnd){candidate=i;break;}
        qreal previousResourceSlot=0;
        if(isResourceRegion(candidate)){
            const auto block=document()->findBlock(regions[candidate].start);
            const qreal top=document()->documentLayout()->blockBoundingRect(block).top();
            const auto last=document()->findBlock(regions[candidate].end);
            previousResourceSlot=(last.next().isValid()?document()->documentLayout()->blockBoundingRect(last.next()).top():document()->size().height()-document()->documentMargin())-top;
        }
        const int topRegion=regionAt(cursorForPosition(QPoint(0,0)).position());
        const int topSource=topRegion>=0?regions[topRegion].sourceStart:-1;
        const qreal topOffset=topRegion>=0?document()->documentLayout()->blockBoundingRect(document()->findBlock(regions[topRegion].start)).top()-scroll:0;
        const int preferredX=movingVertically?textCursor().verticalMovementX():-1;
        if(renderedFont!=font()||renderedBaseUrl!=document()->baseUrl()){
            renderedMarkdown.clear();
            renderedFont=font();
            renderedBaseUrl=document()->baseUrl();
        }
        // Give each source blank line its own editable block. Keep fenced
        // code and display formulas intact for their respective renderers.
        // Build real block geometry before scrolling. Estimated placeholders
        // change both the document height and visible content when replaced.
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
        // Retain the working document's fragments across caret changes. A
        // fixed 128-entry LRU thrashes when a document has more than 128 units.
        renderedMarkdown.setMaxCost(qBound(128,int(units.size())*4,8192));
        // Recover chosen unit by source position after edits changed the boundaries.
        int target=forcedTarget>=0?forcedTarget:(chosen>=0&&chosen<regions.size()?regions[chosen].sourceStart+offset:-1);
        // Defer layout until all fragments and formats have been installed.
        // Otherwise each insertion lays out the growing document again.
        const bool layoutEnabled=document()->isLayoutEnabled();
        document()->setLayoutEnabled(false);
        active=-1;regions.clear();document()->clear();QTextCursor out(document());
        for(qsizetype unitIndex=0;unitIndex<units.size();++unitIndex){
            const auto &unit=units[unitIndex];
            int i=regions.size();bool editing=target>=unit.first&&(target<unit.second||unitIndex==units.size()-1)&&active<0;
            if(i)out.insertBlock(QTextBlockFormat(),QTextCharFormat());int from=out.position();
            QString text=source.mid(unit.first,unit.second-unit.first);
            // The boundary between units already inserts one newline.
            if(text.endsWith('\n'))text.chop(1);
            const auto resourceNodes=mdedit::MarkdownParser::parse(text);
            if(resourceNodes.size()==1&&resourceNodes.first().resource&&resourceSourceEditStart!=unit.first)editing=false;
            const int contentEnd=unit.first+text.size();
            // Native tables stay as QTextTable objects, including when the
            // caret is inside them. Their edits are serialized into the buffer.
            if(editing&&mdedit::readMarkdownTable(text).error.isEmpty()){
                const auto *view=renderedDocument(text,false);
                for(auto block=view->begin();block.isValid();block=block.next())
                    if(QTextCursor(block).currentTable()){editing=false;break;}
            }
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
                    regions.last().listItem=true;
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
                    regions.last().listItem=item!=listItems.cend();
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
                QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
                auto format=bodyStyle.begin().blockFormat();setBodyLineHeight(format,font());
                out.setBlockFormat(format);
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
        layoutNativeTables();
        styleActiveSource();
        document()->setLayoutEnabled(layoutEnabled);
        const bool editingResource=isResourceRegion(active);
        resourceEditSlotHeight=editingResource?previousResourceSlot:0;
        preserveResourceSlot();
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));caret.setVerticalMovementX(preferredX);setTextCursor(caret);}
        if(target>=0&&active<0)for(const auto &region:regions){
            if(target>=region.sourceStart&&target<region.sourceEnd){
                QTextCursor caret(document());caret.setPosition(target>=region.contentEnd?region.end:region.start);
                for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next()){
                    auto *table=QTextCursor(block).currentTable();if(!table)continue;
                    const QString prefix=source.mid(region.sourceStart,qMax(0,target-region.sourceStart));
                    const int line=prefix.count('\n');const int row=qBound(0,line>0?line-1:0,table->rows()-1);
                    const QString rawLine=prefix.section('\n',-1);int pipes=0;bool escape=false;
                    for(QChar ch:rawLine){if(ch=='|'&&!escape)++pipes;if(ch=='\\')escape=!escape;else escape=false;}
                    const int column=qBound(0,pipes-1,table->columns()-1);
                    caret=table->cellAt(row,column).firstCursorPosition();break;
                }
                setTextCursor(caret);break;
            }
        }
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);
        if((backgroundRefresh||target<0||wasEditingResource||editingResource)&&topSource>=0){
            for(const auto &region:regions)if(region.sourceStart==topSource){
                const qreal top=document()->documentLayout()->blockBoundingRect(document()->findBlock(region.start)).top();
                verticalScrollBar()->setValue(qRound(top-topOffset));break;
            }
        }else if(forcedTarget>=0)ensureCursorVisible();
        else if(target>=0)verticalScrollBar()->setValue(scroll+cursorRect().top()-caretY);
        rebuilding=false;refreshLineNumbers();
    }

namespace {
std::optional<mdedit::BlockViewNode> tableNodeAt(const QString &source,int offset){
    for(const auto &node:mdedit::MarkdownParser::parse(source)){
        if(offset<node.source.begin||offset>node.source.end)continue;
        if(node.kind==mdedit::BlockKind::Table)return node;
        if(node.resource&&QFileInfo(QUrl(node.resource->uri).path()).suffix().compare("csv",Qt::CaseInsensitive)==0)return node;
    }
    return {};
}
}
bool LiveEditor::editTableAt(int offset){
    const auto node=tableNodeAt(source,offset);if(!node)return false;
    if(auto *existing=findChild<mdedit::TableEditorDialog *>()){existing->show();existing->raise();return true;}
    auto *dialog=new mdedit::TableEditorDialog(this);
    if(node->resource){
        const QUrl url=mdedit::ResourceResolver::resolve(node->resource->uri,document()->baseUrl());
        if(!url.isLocalFile()){delete dialog;return false;}
        dialog->setWindowTitle("编辑 CSV · "+node->resource->alt);dialog->loadCsv(url.toLocalFile());
        connect(dialog,&mdedit::TableEditorDialog::csvSaved,this,[this]{renderedMarkdown.clear();resourceManager.clear();});
    }else{
        const QString expected=source;
        dialog->setMarkdown(source.mid(node->source.begin,node->source.end-node->source.begin));
        dialog->applyMarkdown=[this,expected,node](const QString &text){
            if(source!=expected)return false;
            const int scroll=verticalScrollBar()->value();
            source.replace(node->source.begin,node->source.end-node->source.begin,text);
            rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
        };
    }
    connect(dialog,&QDialog::finished,this,[this]{
        QTimer::singleShot(0,this,[this]{const int scroll=verticalScrollBar()->value();rebuild(-1,0);verticalScrollBar()->setValue(scroll);});
    });
    dialog->open();return true;
}
bool LiveEditor::moveTableBlockAt(int offset,int direction){
    if(direction!=1&&direction!=-1)return false;
    if(!tableNodeAt(source,offset))return false;
    const auto nodes=mdedit::MarkdownParser::parse(source);int index=-1;
    for(int i=0;i<nodes.size();++i)if(offset>=nodes[i].source.begin&&offset<=nodes[i].source.end){index=i;break;}
    if(index<0||index+direction<0||index+direction>=nodes.size())return false;
    const auto &a=nodes[qMin(index,index+direction)],&b=nodes[qMax(index,index+direction)];
    const QString first=source.mid(a.source.begin,a.source.end-a.source.begin),second=source.mid(b.source.begin,b.source.end-b.source.begin);
    const QString gap=source.mid(a.source.end,b.source.begin-a.source.end);
    source.replace(a.source.begin,b.source.end-a.source.begin,second+gap+first);
    const int scroll=verticalScrollBar()->value();rebuild(-1,0);verticalScrollBar()->setValue(scroll);
    if(sourceChanged)sourceChanged(source);return true;
}
namespace {
constexpr int sourceCellProperty=QTextFormat::UserProperty+41;
QString cellMarkdown(const QTextTableCell &cell){
    auto cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);
    if(cell.firstCursorPosition().blockFormat().boolProperty(sourceCellProperty))
        return cursor.selectedText().replace(QChar::ParagraphSeparator,QStringLiteral("<br>"));
    QString markdown=QTextDocumentFragment(cursor).toMarkdown().trimmed();
    for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next())
        for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(!format.isImageFormat()||!format.hasProperty(formulaMarkdown))continue;
            const QString name=format.toImageFormat().name();
            const QRegularExpression image("!\\[[^\\]]*\\]\\("+QRegularExpression::escape(name)+"[^)]*\\)");
            const auto match=image.match(markdown);
            if(match.hasMatch())markdown.replace(match.capturedStart(),match.capturedLength(),format.property(formulaMarkdown).toString());
        }
    return markdown;
}
QList<QStringList> tableExports(QTextTable *table){
    QList<QStringList> result;
    for(int r=0;r<table->rows();++r){QStringList row;for(int c=0;c<table->columns();++c)row.append(cellMarkdown(table->cellAt(r,c)));result.append(row);}
    return result;
}
}
bool LiveEditor::formatNativeSelection(const QString &left,const QString &right){
    if(!textCursor().currentTable()||!bindNativeTable(textCursor())||!nativeSelection())return false;
    auto cursor=textCursor();
    if(cursor.blockFormat().boolProperty(sourceCellProperty)){
        const QString selected=cursor.selectedText();
        cursor.insertText(left+selected+right);setTextCursor(cursor);return true;
    }
    QTextCharFormat format;
    if(left=="**"&&right=="**")format.setFontWeight(cursor.charFormat().fontWeight()>=QFont::Bold?QFont::Normal:QFont::Bold);
    else if(left=="*"&&right=="*")format.setFontItalic(!cursor.charFormat().fontItalic());
    else return false;
    cursor.mergeCharFormat(format);mergeCurrentCharFormat(format);return true;
}
bool LiveEditor::clipboardNativeCell(bool cut){
    if(axisSource>=0&&axisIndex>=0){
        const auto node=tableNodeAt(source,axisSource);if(!node||node->resource)return false;
        const auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
        if(!data.error.isEmpty())return false;QStringList lines;
        if(axisRow){if(axisIndex>=data.rows.size())return false;lines.append(data.rows[axisIndex].join('\t'));}
        else for(const auto &row:data.rows)lines.append(row.value(axisIndex));
        QApplication::clipboard()->setText(lines.join('\n'));
        if(cut){changeMarkdownTableAt(axisSource,axisRow?axisIndex:0,axisRow?0:axisIndex,axisRow?"removeRow":"removeColumn");axisSource=axisIndex=-1;}
        return true;
    }
    if(!textCursor().currentTable()||!bindNativeTable(textCursor())||!nativeSelection())return false;
    auto cursor=textCursor();
    if(!cursor.hasSelection()){const auto cell=nativeTable->cellAt(cursor);cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);}
    QApplication::clipboard()->setText(cursor.selectedText().replace(QChar::ParagraphSeparator,'\n'));
    if(cut)cursor.removeSelectedText();return true;
}
bool LiveEditor::nativeSelection() const{
    if(!nativeTable)return false;
    auto cursor=textCursor();const auto first=nativeTable->cellAt(cursor.selectionStart()),last=nativeTable->cellAt(cursor.selectionEnd());
    return first.isValid()&&last.isValid()&&first.row()==last.row()&&first.column()==last.column();
}
bool LiveEditor::bindNativeTable(const QTextCursor &cursor){
    auto *table=cursor.currentTable();if(!table)return false;
    if(nativeTable==table){revealNativeCell(cursor);return true;}
    const int index=regionAt(table->firstPosition());if(index<0)return false;
    const auto node=tableNodeAt(source,regions[index].sourceStart);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));if(!data.error.isEmpty())return false;
    nativeTable=table;nativeStart=node->source.begin;nativeEnd=node->source.end;nativeData=data;nativeExports=tableExports(table);active=-1;
    revealNativeCell(cursor);
    return true;
}
void LiveEditor::revealNativeCell(const QTextCursor &cursor){
    if(!nativeTable||cursor.hasSelection())return;
    const auto next=nativeTable->cellAt(cursor);
    if(next.isValid()&&next.row()==sourceCellRow&&next.column()==sourceCellColumn)return;
    QScopedValueRollback<bool> guard(rebuilding,true);
    QTextCursor caret=cursor;
    const int index=regionAt(nativeTable->firstPosition());
    const int before=toPlainText().size();
    if(sourceCellRow>=0){
        const auto previous=nativeTable->cellAt(sourceCellRow,sourceCellColumn);
        if(previous.isValid()){
            auto out=previous.firstCursorPosition();out.setPosition(previous.lastPosition(),QTextCursor::KeepAnchor);
            const QString raw=nativeData.rows.value(sourceCellRow).value(sourceCellColumn);
            out.removeSelectedText();
            auto format=out.blockFormat();format.clearProperty(sourceCellProperty);out.setBlockFormat(format);
            const auto *rendered=renderedDocument(raw,false,true);
            for(auto it=rendered->begin().begin();!it.atEnd();++it){
                const auto fragment=it.fragment();const auto f=fragment.charFormat();
                if(f.isImageFormat()){
                    const QUrl url(f.toImageFormat().name());
                    document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                }
                out.insertText(fragment.text(),f);
            }
        }
    }
    sourceCellRow=sourceCellColumn=-1;
    if(next.isValid()){
        const QString raw=nativeData.rows.value(next.row()).value(next.column());
        if(!scanSpans(raw).isEmpty()||!inlineEditingStyles(raw).isEmpty()||!highlightRanges(raw).isEmpty()){
            const bool atEnd=cursor.position()==next.lastPosition();
            const int offset=cursor.position()-next.firstPosition();
            auto out=next.firstCursorPosition();out.setPosition(next.lastPosition(),QTextCursor::KeepAnchor);
            QTextCharFormat normal;normal.setFont(font());out.insertText(raw,normal);
            auto format=out.blockFormat();format.setProperty(sourceCellProperty,true);out.setBlockFormat(format);
            sourceCellRow=next.row();sourceCellColumn=next.column();
            caret=next.firstCursorPosition();caret.setPosition(atEnd?next.lastPosition():next.firstPosition()+qMin(offset,int(raw.size())));
        }
    }
    const int delta=toPlainText().size()-before;
    if(index>=0){
        regions[index].end+=delta;
        for(int i=index+1;i<regions.size();++i){regions[i].start+=delta;regions[i].end+=delta;}
    }
    nativeExports=tableExports(nativeTable);oldText=toPlainText();
    layoutNativeTable(nativeTable);setTextCursor(caret);
}

void LiveEditor::layoutNativeTable(QTextTable *table){
    // Keep short columns compact; cap wide columns evenly when space runs out.
    QList<qreal> preferred(table->columns(),80);
    const auto tableFormat=table->format();
    for(int row=0;row<table->rows();++row)for(int column=0;column<table->columns();++column){
        const auto cell=table->cellAt(row,column);
        auto cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);
        QTextDocument measure;measure.setDefaultFont(font());measure.setDocumentMargin(0);
        auto option=measure.defaultTextOption();option.setWrapMode(QTextOption::NoWrap);measure.setDefaultTextOption(option);
        QTextCursor(&measure).insertFragment(QTextDocumentFragment(cursor));
        // IME preedit lives in QTextLayout, outside the committed document
        // fragment. Include it in measurement without publishing it as source.
        for(auto block=cell.lastCursorPosition().block();block.isValid()&&block.position()>=cell.firstPosition();block=block.previous()){
            const auto *layout=block.layout();
            if(layout->preeditAreaPosition()<0||layout->preeditAreaText().isEmpty())continue;
            QTextCursor composing(&measure);
            composing.setPosition(qBound(0,block.position()+layout->preeditAreaPosition()-cell.firstPosition(),measure.characterCount()-1));
            composing.insertText(layout->preeditAreaText());
        }
        preferred[column]=qMax(preferred[column],qCeil(measure.idealWidth())+2*tableFormat.cellPadding()+2*qMax(1.0,tableFormat.border())+2);
        for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next()){
            auto format=block.blockFormat();format.setAlignment(Qt::AlignLeft);format.setNonBreakableLines(false);
            if(format!=block.blockFormat())QTextCursor(block).setBlockFormat(format);
        }
    }
    const qreal available=qMax(40.0,viewport()->width()-2*document()->documentMargin()-4);
    qreal natural=0,largest=80;for(qreal value:preferred){natural+=value;largest=qMax(largest,value);}
    const qreal width=qMin(available,natural);
    qreal low=0,high=largest;
    for(int step=0;step<40;++step){
        const qreal cap=(low+high)/2; qreal total=0;
        for(qreal value:preferred)total+=qMin(value,cap);
        if(total>width)high=cap;else low=cap;
    }
    const qreal cap=(low+high)/2;
    auto format=table->format();format.setAlignment(Qt::AlignLeft);format.setWidth(QTextLength(QTextLength::FixedLength,width));
    QList<QTextLength> columns;
    for(int column=0;column<table->columns();++column)columns.append(QTextLength(QTextLength::PercentageLength,100.0*qMin(preferred[column],cap)/width));
    format.setColumnWidthConstraints(columns);
    if(format!=table->format())table->setFormat(format);
    auto option=table->document()->defaultTextOption();
    if(option.wrapMode()!=QTextOption::WrapAtWordBoundaryOrAnywhere){option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);table->document()->setDefaultTextOption(option);}
}

void LiveEditor::layoutNativeTables(){
    QScopedValueRollback<bool> guard(rebuilding,true);
    QSet<QTextTable *> seen;
    for(auto block=document()->begin();block.isValid();block=block.next()){
        auto *table=QTextCursor(block).currentTable();
        if(table&&!seen.contains(table)){seen.insert(table);layoutNativeTable(table);}
    }
}

bool LiveEditor::syncNativeTable(){
    if(!nativeTable||nativeStart<0)return false;
    const int index=regionAt(nativeTable->firstPosition());if(index<0)return false;
    const auto current=tableExports(nativeTable);
    if(current==nativeExports){oldText=toPlainText();return true;}
    QList<QStringList> rows;
    for(int r=0;r<current.size();++r){QStringList row;
        for(int c=0;c<current[r].size();++c){
            const bool unchanged=r<nativeExports.size()&&c<nativeExports[r].size()&&current[r][c]==nativeExports[r][c];
            row.append(unchanged&&r<nativeData.rows.size()?nativeData.rows[r].value(c):current[r][c]);
        }rows.append(row);
    }
    nativeData.rows=rows;const QString replacement=mdedit::writeMarkdownTable(nativeData);
    const int delta=replacement.size()-(nativeEnd-nativeStart),visualDelta=toPlainText().size()-oldText.size();
    source.replace(nativeStart,nativeEnd-nativeStart,replacement);nativeEnd+=delta;
    auto &region=regions[index];region.sourceEnd+=delta;region.end+=visualDelta;if(region.contentEnd>=0)region.contentEnd+=delta;
    for(int i=index+1;i<regions.size();++i){regions[i].sourceStart+=delta;regions[i].sourceEnd+=delta;if(regions[i].contentEnd>=0)regions[i].contentEnd+=delta;regions[i].start+=visualDelta;regions[i].end+=visualDelta;}
    {QScopedValueRollback<bool> guard(rebuilding,true);layoutNativeTable(nativeTable);}
    nativeExports=current;oldText=toPlainText();refreshLineNumbers();if(sourceChanged)sourceChanged(source);return true;
}
bool LiveEditor::changeMarkdownTableAt(int offset,int row,int column,const QString &operation){
    const auto node=tableNodeAt(source,offset);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
    if(!data.error.isEmpty()||row<0||row>=data.rows.size())return false;
    int columns=1;for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));
    if(column<0||column>=columns)return false;
    for(auto &cells:data.rows)while(cells.size()<columns)cells.append(QString());
    while(data.separators.size()<columns)data.separators.append("---");
    if(operation=="insertRowBefore"||operation=="insertRowAfter"){
        if(data.rows.size()>=10000)return false;
        QStringList cells;for(int c=0;c<columns;++c)cells.append(QString());
        data.rows.insert(row+(operation=="insertRowAfter"?1:0),cells);
    }else if(operation=="removeRow"){
        if(data.rows.size()<=1)return false;data.rows.removeAt(row);
    }else if(operation=="insertColumnBefore"||operation=="insertColumnAfter"){
        if(columns>=256)return false;const int at=column+(operation=="insertColumnAfter"?1:0);
        for(auto &cells:data.rows)cells.insert(at,QString());data.separators.insert(at,"---");
    }else if(operation=="removeColumn"){
        if(columns<=1)return false;for(auto &cells:data.rows)cells.removeAt(column);data.separators.removeAt(column);
    }else if(operation=="moveRowUp"||operation=="moveRowDown"){
        const int to=row+(operation=="moveRowUp"?-1:1);if(to<0||to>=data.rows.size())return false;data.rows.swapItemsAt(row,to);
    }else if(operation=="moveColumnLeft"||operation=="moveColumnRight"){
        const int to=column+(operation=="moveColumnLeft"?-1:1);if(to<0||to>=columns)return false;
        for(auto &cells:data.rows)cells.swapItemsAt(column,to);data.separators.swapItemsAt(column,to);
    }else return false;
    const int scroll=verticalScrollBar()->value();
    source.replace(node->source.begin,node->source.end-node->source.begin,mdedit::writeMarkdownTable(data));
    rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}
void LiveEditor::mouseDoubleClickEvent(QMouseEvent *event){
    const auto cursor=cursorForPosition(event->position().toPoint());
    const auto *table=cursor.currentTable();
    const int region=table?regionAt(table->firstPosition()):-1;
    const int offset=region>=0?regions[region].sourceStart:sourcePosition(cursor.position());
    const auto node=tableNodeAt(source,offset);
    if(event->button()==Qt::LeftButton&&node&&!node->resource){
        bindNativeTable(cursor);
        QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::mouseDoubleClickEvent(event);event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&editTableAt(offset)){event->accept();return;}
    QTextBrowser::mouseDoubleClickEvent(event);
}
void LiveEditor::contextMenuEvent(QContextMenuEvent *event){
    const auto cursor=cursorForPosition(event->pos());
    const auto *table=cursor.currentTable();
    const int region=table?regionAt(table->firstPosition()):-1;
    const int offset=region>=0?regions[region].sourceStart:sourcePosition(cursor.position());
    const auto node=tableNodeAt(source,offset);
    if(node){
        auto *menu=new QMenu(this);menu->setObjectName("tableContextMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
        if(!node->resource)menu->addAction("编辑当前单元格",this,[this,cursor]{setTextCursor(cursor);bindNativeTable(cursor);setFocus();});
        menu->addAction(node->resource?"编辑 CSV…":"表格编辑面板…",this,[this,offset]{editTableAt(offset);});menu->addSeparator();
        if(!node->resource){
            int row=0,column=0,rows=1,columns=1;
            if(const auto *table=cursor.currentTable()){
                const auto cell=table->cellAt(cursor);row=cell.row();column=cell.column();rows=table->rows();columns=table->columns();
            }else{
                const auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
                rows=data.rows.size();for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));
                const int line=source.mid(node->source.begin,qMax(0,offset-node->source.begin)).count('\n');
                row=qBound(0,line>0?line-1:0,qMax(0,rows-1));
                const auto rawLine=source.mid(node->source.begin,node->source.end-node->source.begin).section('\n',line,line);
                const int lineStart=offset-(offset==0?0:source.lastIndexOf('\n',offset-1)+1);
                column=qBound(0,rawLine.left(lineStart).count('|')-1,qMax(0,columns-1));
            }
            auto operation=[&](const QString &label,const QString &name,bool enabled=true){
                auto *action=menu->addAction(label,this,[this,offset,row,column,name]{changeMarkdownTableAt(offset,row,column,name);});
                action->setObjectName(name);action->setEnabled(enabled);
            };
            operation("在上方插入行","insertRowBefore");operation("在下方插入行","insertRowAfter");
            operation("在左侧插入列","insertColumnBefore");operation("在右侧插入列","insertColumnAfter");menu->addSeparator();
            operation("当前行上移","moveRowUp",row>0);operation("当前行下移","moveRowDown",row+1<rows);
            operation("当前列左移","moveColumnLeft",column>0);operation("当前列右移","moveColumnRight",column+1<columns);menu->addSeparator();
            operation("删除当前行","removeRow",rows>1);operation("删除当前列","removeColumn",columns>1);menu->addSeparator();
        }
        menu->addAction("表格块上移",this,[this,offset]{moveTableBlockAt(offset,-1);});
        menu->addAction("表格块下移",this,[this,offset]{moveTableBlockAt(offset,1);});menu->popup(event->globalPos());event->accept();return;
    }
    auto *menu=createStandardContextMenu();menu->setAttribute(Qt::WA_DeleteOnClose);menu->popup(event->globalPos());
}

QList<LiveEditor::AxisControls> LiveEditor::axisControls() const{
    QList<AxisControls> result;QSet<QTextTable *> seen;
    const QPointF scroll(horizontalScrollBar()->value(),verticalScrollBar()->value());
    auto *layout=document()->documentLayout();
    for(auto block=document()->begin();block.isValid();block=block.next()){
        auto *table=QTextCursor(block).currentTable();if(!table||seen.contains(table))continue;seen.insert(table);
        const int region=regionAt(table->firstPosition());if(region<0)continue;
        AxisControls item;item.table=table;item.start=regions[region].sourceStart;
        const auto format=table->format();
        auto cellBounds=[&](const QTextTableCell &cell){
            QRectF rect;
            for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next()){
                auto bounds=layout->blockBoundingRect(block);
                // blockBoundingRect measures the glyph layout. Fixed line
                // spacing also occupies space below it inside the cell.
                const auto paragraph=block.blockFormat();
                if(paragraph.lineHeightType()==QTextBlockFormat::FixedHeight)
                    bounds.setHeight(qMax(bounds.height(),paragraph.lineHeight()*qMax(1,block.layout()->lineCount())));
                rect=rect.isNull()?bounds:rect.united(bounds);
            }
            const auto cellFormat=cell.format().toTableCellFormat();
            auto padding=[&](int property,qreal value){return cellFormat.hasProperty(property)?value:format.cellPadding();};
            const qreal border=format.borderCollapse()?qMax(1.0,format.border())/2:format.border();
            rect.adjust(-padding(QTextFormat::TableCellLeftPadding,cellFormat.leftPadding())-border,
                        -padding(QTextFormat::TableCellTopPadding,cellFormat.topPadding())-border,
                         padding(QTextFormat::TableCellRightPadding,cellFormat.rightPadding())+border,
                         padding(QTextFormat::TableCellBottomPadding,cellFormat.bottomPadding())+border);
            return rect.translated(-scroll);
        };
        for(int r=0;r<table->rows();++r){
            QRectF rect;
            for(int c=0;c<table->columns();++c){const auto bounds=cellBounds(table->cellAt(r,c));rect=rect.isNull()?bounds:rect.united(bounds);}
            item.rows.append(rect);item.frame=item.frame.isNull()?rect:item.frame.united(rect);
        }
        if(item.frame.bottom()<0||item.frame.top()>viewport()->height())continue;
        for(int c=0;c<table->columns();++c){
            const auto rect=cellBounds(table->cellAt(0,c));
            item.columns.append(QRectF(rect.left(),item.frame.top(),rect.width(),item.frame.height()));
        }
        for(auto &rect:item.rows){rect.setLeft(item.frame.left());rect.setRight(item.frame.right());}
        item.addRow=QRectF(item.frame.left(),item.frame.bottom()+3,item.frame.width(),18);
        item.addColumn=QRectF(item.frame.right()+3,item.frame.top(),18,item.frame.height());result.append(item);
    }return result;
}
void LiveEditor::updateAxisDrop(){
    if(source!=axisSnapshot){cancelAxisDrag();return;}
    for(const auto &item:axisControls())if(item.start==axisSource){
        const auto &rects=axisRow?item.rows:item.columns;qreal nearest=std::numeric_limits<qreal>::max();axisBoundary=-1;
        for(int i=0;i<=rects.size();++i){const qreal position=i<rects.size()?(axisRow?rects[i].top():rects[i].left()):(axisRow?rects.last().bottom():rects.last().right());
            const qreal distance=qAbs((axisRow?axisPoint.y():axisPoint.x())-position);
            if(distance<nearest){nearest=distance;axisBoundary=i;axisDropPosition=position;}}
        return;
    }
}
void LiveEditor::cancelAxisDrag(){axisPressed=axisStarted=false;axisBoundary=-1;axisDragScroll.stop();viewport()->unsetCursor();viewport()->update();}
bool LiveEditor::reorderMarkdownAxisAt(int offset,bool row,int from,int boundary){
    const auto node=tableNodeAt(source,offset);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));if(!data.error.isEmpty())return false;
    int columns=1;for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));const int count=row?data.rows.size():columns;
    if(from<0||from>=count||boundary<0||boundary>count||boundary==from||boundary==from+1)return false;
    const int to=boundary>from?boundary-1:boundary;
    if(row)data.rows.move(from,to);
    else{for(auto &cells:data.rows){while(cells.size()<columns)cells.append(QString());cells.move(from,to);}while(data.separators.size()<columns)data.separators.append("---");data.separators.move(from,to);}
    const int scroll=verticalScrollBar()->value();source.replace(node->source.begin,node->source.end-node->source.begin,mdedit::writeMarkdownTable(data));
    rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}
bool LiveEditor::moveMarkdownTableTo(int offset,int boundary){
    const auto nodes=mdedit::MarkdownParser::parse(source);int from=-1;
    for(int i=0;i<nodes.size();++i)if(nodes[i].kind==mdedit::BlockKind::Table&&offset>=nodes[i].source.begin&&offset<=nodes[i].source.end){from=i;break;}
    if(from<0||boundary<0||boundary>nodes.size()||boundary==from||boundary==from+1)return false;
    QStringList blocks,gaps;
    for(int i=0;i<nodes.size();++i){blocks.append(source.mid(nodes[i].source.begin,nodes[i].source.end-nodes[i].source.begin));
        if(i+1<nodes.size())gaps.append(source.mid(nodes[i].source.end,nodes[i+1].source.begin-nodes[i].source.end));}
    const int to=boundary>from?boundary-1:boundary;const QString table=blocks.takeAt(from);blocks.insert(to,table);
    QList<int> order;for(int i=0;i<nodes.size();++i)order.append(i);order.move(from,to);
    QString result=source.left(nodes.first().source.begin);
    for(int i=0;i<blocks.size();++i){result+=blocks[i];if(i<gaps.size())result+=(order[i+1]==order[i]+1||gaps[i].contains("\n\n"))?gaps[i]:QString("\n\n");}
    result+=source.mid(nodes.last().source.end);const int scroll=verticalScrollBar()->value();
    source=result;rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}

QList<LiveEditor::BlockControls> LiveEditor::blockControls() const{
    QList<BlockControls> result;
    if(sourceExposed)return result;
    const QFontMetricsF metrics(font());
    const qreal width=qBound(24.0,metrics.height()+4,30.0);
    const qreal height=width;
    auto *layout=document()->documentLayout();
    for(int i=0;i<regions.size();++i){
        const auto &r=regions[i];
        if(r.inlinePart)continue;
        const QString raw=source.mid(r.sourceStart,r.sourceEnd-r.sourceStart);
        const auto nodes=mdedit::MarkdownParser::parse(raw);
        const auto resource=nodes.size()==1?nodes.first().resource:std::nullopt;
        if(!resource)continue;
        const auto first=document()->findBlock(r.start);
        if(!first.isValid())continue;
        const QRectF bounds=layout->blockBoundingRect(first);
        const qreal y=bounds.top()-verticalScrollBar()->value();
        if(y+height<0||y>viewport()->height())continue;
        const qreal right=qMin(qreal(viewport()->width()-8),bounds.right()-horizontalScrollBar()->value());
        result.append({i,QRectF(right-2*width-6,y,width,height),QRectF(right-width,y,width,height),
            resource?mdedit::ResourceResolver::resolve(resource->uri,document()->baseUrl()):QUrl()});
    }
    return result;
}

QList<LiveEditor::CodeCopyControl> LiveEditor::codeCopyControls() const{
    QList<CodeCopyControl> result;
    if(sourceExposed)return result;
    const QFontMetricsF metrics(font());
    const qreal width=qBound(24.0,metrics.height()+4,30.0);
    const qreal height=width;
    auto *layout=document()->documentLayout();
    for(int i=0;i<regions.size();++i){
        const auto &region=regions[i];
        if(!region.codeBlock)continue;
        const auto first=document()->findBlock(region.start);
        if(!first.isValid())continue;
        const auto bounds=layout->blockBoundingRect(first);
        const qreal top=bounds.top()-verticalScrollBar()->value();
        if(top+height<0||top>viewport()->height())continue;
        const qreal right=qMin(qreal(viewport()->width()-8),bounds.right()-horizontalScrollBar()->value()-6);
        result.append({i,QRectF(right-width,top,width,height)});
    }
    return result;
}

void LiveEditor::mouseMoveEvent(QMouseEvent *event){
        tableHoverPoint=event->position().toPoint();viewport()->update();
        for(const auto &item:codeCopyControls())if(item.rect.contains(tableHoverPoint)){
            viewport()->setCursor(Qt::PointingHandCursor);viewport()->setToolTip("复制代码");return;
        }
        for(const auto &item:blockControls())if(item.open.contains(tableHoverPoint)||item.edit.contains(tableHoverPoint)){
            const bool disabled=item.open.contains(tableHoverPoint)&&item.file.isEmpty();
            viewport()->setCursor(disabled?Qt::ArrowCursor:Qt::PointingHandCursor);
            viewport()->setToolTip(disabled?"此块没有关联的原文件":item.open.contains(tableHoverPoint)?"打开原文件":"编辑此块的 Markdown 原代码");return;
        }
        if(axisPressed){
            axisPoint=tableHoverPoint;
            if(!axisStarted&&(axisPoint-axisOrigin).manhattanLength()>=QApplication::startDragDistance()){axisStarted=true;axisDragScroll.start();viewport()->setCursor(Qt::ClosedHandCursor);}
            if(axisStarted)updateAxisDrop();event->accept();return;
        }
        for(const auto &item:axisControls()){
            if(item.addRow.contains(tableHoverPoint)||item.addColumn.contains(tableHoverPoint)){viewport()->setCursor(Qt::PointingHandCursor);viewport()->setToolTip(item.addRow.contains(tableHoverPoint)?"追加一行":"追加一列");return;}
            for(const auto &rect:item.rows)if(QRectF(item.frame.left()-22,rect.top()-2,23,rect.height()+4).contains(tableHoverPoint)){viewport()->setCursor(Qt::OpenHandCursor);viewport()->setToolTip("选择并拖动这一行");return;}
            for(const auto &rect:item.columns)if(QRectF(rect.left()-2,item.frame.top()-22,rect.width()+4,23).contains(tableHoverPoint)){viewport()->setCursor(Qt::OpenHandCursor);viewport()->setToolTip("选择并拖动这一列");return;}
        }
        viewport()->setToolTip({});
        QTextBrowser::mouseMoveEvent(event);
        if(event->buttons()==Qt::NoButton){
            const bool overLink=!clickableAnchorAt(event->position().toPoint()).isEmpty();
            viewport()->setCursor(overLink?Qt::PointingHandCursor:Qt::IBeamCursor);
        }
    }

void LiveEditor::mousePressEvent(QMouseEvent *event){
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            for(const auto &item:codeCopyControls())if(item.rect.contains(event->position())){
                const auto &region=regions[item.region];
                const QString raw=source.mid(region.sourceStart,region.contentEnd-region.sourceStart);
                const auto body=codeBody(raw,region.codeFenceStart);
                QApplication::clipboard()->setText(raw.mid(body.start,body.end-body.start));
                copiedCodeSource=region.sourceStart;copyFeedbackTimer.start();viewport()->update();
                event->accept();return;
            }
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            for(const auto &item:blockControls()){
                if(item.open.contains(event->position())){
                    if(!item.file.isEmpty())QDesktopServices::openUrl(item.file);
                    event->accept();return;
                }
                if(item.edit.contains(event->position())){
                    editResourceSourceAt(regions[item.region].sourceStart);setFocus();event->accept();return;
                }
            }
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            const QPoint point=event->position().toPoint();
            for(const auto &item:axisControls()){
                if(item.addRow.contains(point)||item.addColumn.contains(point)){
                    axisSource=axisIndex=-1;
                    changeMarkdownTableAt(item.start,item.rows.size()-1,item.columns.size()-1,item.addRow.contains(point)?"insertRowAfter":"insertColumnAfter");event->accept();return;
                }
                for(int axis=0;axis<2;++axis){const auto &rects=axis==0?item.rows:item.columns;
                    for(int i=0;i<rects.size();++i){const QRectF handle=axis==0?QRectF(item.frame.left()-22,rects[i].top()-2,23,rects[i].height()+4):QRectF(rects[i].left()-2,item.frame.top()-22,rects[i].width()+4,23);
                        if(!handle.contains(point))continue;
                        axisSource=item.start;axisIndex=i;axisRow=axis==0;axisPressed=true;axisStarted=false;axisSnapshot=source;axisOrigin=axisPoint=point;
                        const auto first=item.table->cellAt(axisRow?i:0,axisRow?0:i),last=item.table->cellAt(axisRow?i:item.table->rows()-1,axisRow?item.table->columns()-1:i);
                        auto selection=first.firstCursorPosition();bindNativeTable(selection);selection.setPosition(last.lastPosition(),QTextCursor::KeepAnchor);
                        const int sy=verticalScrollBar()->value(),sx=horizontalScrollBar()->value();
                        {QScopedValueRollback<bool> guard(movingCursor,true);setTextCursor(selection);}
                        verticalScrollBar()->setValue(sy);horizontalScrollBar()->setValue(sx);
                        viewport()->update();event->accept();return;
                    }
                }
            }
            axisSource=axisIndex=-1;
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
        }
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
            const int resourceRegion=regionAt(cursorForPosition(point).position());
            if(isResourceRegion(resourceRegion)&&resourceRegion!=active){event->accept();return;}
            if(!isResourceRegion(resourceRegion))resourceSourceEditStart=-1;
            const int hit=cursorForPosition(point).position();
            const auto nativeCursor=cursorForPosition(point);
            if(nativeCursor.currentTable()&&bindNativeTable(nativeCursor)){
                QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::mousePressEvent(event);return;
            }
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

void LiveEditor::mouseReleaseEvent(QMouseEvent *event){
        if(event->button()==Qt::LeftButton&&axisPressed){
            const int offset=axisSource,from=axisIndex,boundary=axisBoundary;const bool row=axisRow;
            bool inside=false;for(const auto &item:axisControls())if(item.start==axisSource)inside=item.frame.adjusted(-25,-25,25,25).contains(event->position());
            const bool apply=axisStarted&&source==axisSnapshot&&inside&&viewport()->rect().contains(event->position().toPoint());
            cancelAxisDrag();if(apply){reorderMarkdownAxisAt(offset,row,from,boundary);axisIndex=boundary>from?boundary-1:boundary;}event->accept();return;
        }
        if(event->button()==Qt::LeftButton&&!pressedLink.isEmpty()){
            const QString anchor=pressedLink;
            pressedLink.clear();
            const auto point=event->position().toPoint();
            event->accept();
            if(event->modifiers()==Qt::NoModifier&&anchorAt(point)==anchor&&
               (point-linkPressPosition).manhattanLength()<QApplication::startDragDistance())
                {if(QUrl(anchor).scheme()=="mdedit-pdf")handlePdfAction(QUrl(anchor));
                 else emit anchorClicked(QUrl(anchor));}
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }

void LiveEditor::focusInEvent(QFocusEvent *event){
        QTextBrowser::focusInEvent(event);
        if(event->reason()!=Qt::ActiveWindowFocusReason&&active<0)activateCaretRegion();
    }

void LiveEditor::focusOutEvent(QFocusEvent *event){
        if(axisPressed)cancelAxisDrag();
        QTextBrowser::focusOutEvent(event);
        // Menus and dialogs take focus before their actions read the selection.
        // Rebuilding here discards it and moves the cursor to the document end.
        // The next caret movement will switch the active region when needed.
    }

void LiveEditor::keyPressEvent(QKeyEvent *event){
        if(axisPressed&&event->key()==Qt::Key_Escape){cancelAxisDrag();event->accept();return;}
        if(axisSource>=0&&axisIndex>=0&&!axisPressed&&(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)){
            changeMarkdownTableAt(axisSource,axisRow?axisIndex:0,axisRow?0:axisIndex,axisRow?"removeRow":"removeColumn");axisSource=axisIndex=-1;event->accept();return;
        }
        // A selected native table is a structural selection, not a cell edit.
        if(textCursor().hasSelection()&&
           (event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete)){
            const auto cursor=textCursor();
            int start=-1,end=-1;
            if(cursor.selectionStart()==0&&cursor.selectionEnd()==document()->characterCount()-1){
                start=0;end=source.size();
            }else if(cursor.currentTable()&&bindNativeTable(cursor)){
                int row,count,column,columns;
                cursor.selectedTableCells(&row,&count,&column,&columns);
                if(row==0&&column==0&&count==nativeTable->rows()&&columns==nativeTable->columns()){
                    start=nativeStart;end=nativeEnd;
                }
            }
            if(start>=0){
                source.remove(start,end-start);
                rebuild(-1,0,start);
                if(sourceChanged)sourceChanged(source);
                event->accept();return;
            }
        }
        QScopedValueRollback<bool> verticalNavigation(movingVertically,event->key()==Qt::Key_Up||event->key()==Qt::Key_Down);
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){
                if(history)history(event->matches(QKeySequence::Redo));event->accept();return;
            }
            const auto cell=nativeTable->cellAt(textCursor());
            const bool leaveAbove=event->key()==Qt::Key_Up&&cell.row()==0&&cursorRect().top()<=cursorRect(cell.firstCursorPosition()).top();
            const bool leaveBelow=event->key()==Qt::Key_Down&&cell.row()+1==nativeTable->rows()&&cursorRect().bottom()>=cursorRect(cell.lastCursorPosition()).bottom();
            if((leaveAbove||leaveBelow)&&event->modifiers()==Qt::NoModifier&&!textCursor().hasSelection()){
                QTextCursor outside(document());outside.setPosition(leaveAbove?qMax(0,nativeTable->firstPosition()-2):qMin(document()->characterCount()-1,nativeTable->lastPosition()+2));
                {QScopedValueRollback<bool> guard(movingCursor,true);setTextCursor(outside);}
                revealNativeCell(textCursor());nativeTable.clear();activateCaretRegion();event->accept();return;
            }
            if(event->key()==Qt::Key_Tab||event->key()==Qt::Key_Backtab){
                const bool backwards=event->key()==Qt::Key_Backtab||(event->modifiers()&Qt::ShiftModifier);
                int next=cell.row()*nativeTable->columns()+cell.column()+(backwards?-1:1);
                if(next>=nativeTable->rows()*nativeTable->columns())nativeTable->appendRows(1);
                if(next>=0)setTextCursor(nativeTable->cellAt(next/nativeTable->columns(),next%nativeTable->columns()).firstCursorPosition());
                else{
                    QTextCursor outside(document());outside.setPosition(qMax(0,nativeTable->firstPosition()-2));
                    {QScopedValueRollback<bool> guard(movingCursor,true);setTextCursor(outside);}
                    revealNativeCell(textCursor());nativeTable.clear();activateCaretRegion();
                }
                event->accept();return;
            }
            // Crossing a cell boundary must not merge cells or remove the table.
            if(!textCursor().hasSelection()&&((event->key()==Qt::Key_Backspace&&textCursor().position()==cell.firstPosition())||
                (event->key()==Qt::Key_Delete&&textCursor().position()==cell.lastPosition()))){event->accept();return;}
            {QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::keyPressEvent(event);}
            activateCaretRegion();
            return;
        }
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
        if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){
            if(history)history(event->matches(QKeySequence::Redo));
            event->accept();return;
        }
        // A plain newline deletion has no grapheme or word-boundary rules to
        // delegate to QTextControl. Apply it to the authoritative source before
        // activating a region: activation and exposeSource would each rebuild
        // the whole document before the final preview rebuild below.
        if(!textCursor().hasSelection()&&event->modifiers()==Qt::NoModifier&&
           (event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete)){
            const int at=sourcePosition(textCursor().position());
            const int removeAt=event->key()==Qt::Key_Backspace?at-1:at;
            if(removeAt>=0&&removeAt<source.size()&&source[removeAt]=='\n'){
                source.remove(removeAt,1);
                rebuild(-1,0,removeAt);
                verticalScrollBar()->setValue(scrollBeforeEdit);
                const QRect caret=cursorRect();
                if(caret.top()<0||caret.bottom()>=viewport()->height())ensureCursorVisible();
                if(sourceChanged)sourceChanged(source);
                event->accept();return;
            }
        }
        // Qt can tag native arrow keys with KeypadModifier (including macOS
        // arrows). It identifies the key, not an extra navigation command.
        const auto navigationModifiers=event->modifiers()&~Qt::KeypadModifier;
        const bool horizontalArrow=(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)&&
            navigationModifiers==Qt::NoModifier&&!textCursor().hasSelection();
        auto renderedInline=[this](int i){
            const auto &r=regions[i];
            return i!=active&&r.inlinePart&&r.end>r.start&&
                source.mid(r.sourceStart,r.contentEnd-r.sourceStart)!=oldText.mid(r.start,r.end-r.start);
        };
        auto enterRenderedInline=[&]{
            const int position=textCursor().position();
            for(int i=0;i<regions.size();++i){
                const auto &r=regions[i];
                if(!renderedInline(i))continue;
                const bool enterLeft=event->key()==Qt::Key_Right&&position==r.start;
                const bool enterRight=event->key()==Qt::Key_Left&&position==r.end;
                if(!enterLeft&&!enterRight)continue;
                const QString raw=source.mid(r.sourceStart,r.contentEnd-r.sourceStart);
                int contentStart=0,contentEnd=raw.size();
                const auto spans=scanSpans(raw);
                if(spans.size()==1&&spans.first().start==0&&spans.first().length==raw.size()){
                    // Code and math scanners include their delimiters. Enter
                    // their source bodies directly, including multi-backtick code.
                    int delimiter=1;
                    while(delimiter<raw.size()&&raw[delimiter]==raw[0])++delimiter;
                    contentStart=delimiter;contentEnd=raw.size()-delimiter;
                }else{
                    // Emphasis, strikeout and highlighting styles already
                    // describe content ranges without the surrounding markers.
                    const auto styles=inlineEditingStyles(raw);
                    if(!styles.isEmpty()){
                        contentStart=raw.size();contentEnd=0;
                        for(const auto &style:styles){
                            contentStart=qMin(contentStart,style.start);
                            contentEnd=qMax(contentEnd,style.start+style.length);
                        }
                    }
                }
                const int offset=enterLeft?contentStart:contentEnd;
                // Expand at the content edge without taking a visual step,
                // which would consume the first or last content character.
                rebuild(i,offset);
                QScopedValueRollback<bool> guard(movingCursor,true);
                auto caret=textCursor();
                caret.setPosition(regions[active].start+offset);
                setTextCursor(caret);
                event->accept();return true;
            }
            return false;
        };
        if(horizontalArrow&&enterRenderedInline())return;
        if(active<0)activateCaretRegion();
        if(active<0){event->ignore();return;}
        if((event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)&&
            !(navigationModifiers&~Qt::ShiftModifier)){
            const auto selection=sourceSelection();const int at=selection.second;
            QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme,source);boundary.setPosition(at);
            int next=event->key()==Qt::Key_Left?boundary.toPreviousBoundary():boundary.toNextBoundary();
            if(next>=0){
                const auto here=listPrefixAt(at),there=listPrefixAt(next);
                if(here||there){
                    if(event->modifiers()&Qt::ShiftModifier)exposeSource(selection.first,next);
                    else{
                        if(selection.first!=selection.second)next=event->key()==Qt::Key_Left?
                            qMin(selection.first,selection.second):qMax(selection.first,selection.second);
                        rebuild(-1,0,next);
                    }
                    event->accept();return;
                }
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
        // A source line start must stay a line start even when the next
        // rendered block has a different indent or hidden Markdown prefix.
        const int beforeMove=sourcePosition(textCursor().position());
        const bool keepLineStart=movingVertically&&event->modifiers()==Qt::NoModifier&&
            !textCursor().hasSelection()&&(beforeMove==0||source[beforeMove-1]=='\n');
        std::optional<QPair<int,int>> verticalPrefix;
        int prefixColumn=0;
        if((event->key()==Qt::Key_Up||event->key()==Qt::Key_Down)&&
            event->modifiers()==Qt::NoModifier&&!textCursor().hasSelection()){
            const int at=sourcePosition(textCursor().position());
            verticalPrefix=listPrefixAt(at);
            if(verticalPrefix&&at<verticalPrefix->second)prefixColumn=at-verticalPrefix->first;
            else verticalPrefix.reset();
        }
        {
            // QTextControl still uses the old document positions until its key
            // handler returns. Rebuild only after that movement has finished.
            QScopedValueRollback<bool> guard(movingCursor,true);
            if(newline)QTextEdit::keyPressEvent(event);
            else QTextBrowser::keyPressEvent(event);
        }
        // Cross-region edits temporarily use the full source so Qt can apply
        // its usual deletion and selection rules across Markdown boundaries.
        if(keepLineStart){
            auto destination=textCursor();
            destination.movePosition(QTextCursor::StartOfLine);
            int at=sourcePosition(destination.position());
            if(destination.atBlockStart())at=at==0?0:source.lastIndexOf('\n',at-1)+1;
            rebuild(-1,0,at);
            event->accept();return;
        }
        if(verticalPrefix){
            const auto destination=listPrefixAt(sourcePosition(textCursor().position()));
            if(destination&&destination->first!=verticalPrefix->first){
                rebuild(-1,0,destination->first+qMin(prefixColumn,destination->second-destination->first-1));
                event->accept();return;
            }
        }
        if(sourceExposed||deleting)rebuild(-1,0,sourcePosition(textCursor().position()));
        else if(toPlainText()==oldText){
            // Expand on arrival at the inward edge, before generic region
            // activation can map the caret past a content character.
            if(horizontalArrow&&enterRenderedInline())return;
            activateCaretRegion();
        }
        if(deleting){
            // Exposing source and restoring preview can both scroll the cursor
            // into view using temporary geometry. Keep the viewport anchored
            // to the final layout instead of inheriting those scroll changes.
            verticalScrollBar()->setValue(scrollBeforeEdit);
            const QRect caret=cursorRect();
            if(caret.top()<0||caret.bottom()>=viewport()->height())ensureCursorVisible();
        }
    }

void LiveEditor::insertFromMimeData(const QMimeData *mime){
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            textCursor().insertText(mime->text());return;
        }
        prepareSelection();insertPlainText(mime->text());
        if(sourceExposed)rebuild(-1,0,sourcePosition(textCursor().position()));
    }

void LiveEditor::inputMethodEvent(QInputMethodEvent *event){
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            QTextBrowser::inputMethodEvent(event);
            if(nativeTable){QScopedValueRollback<bool> guard(rebuilding,true);layoutNativeTable(nativeTable);}
            return;
        }
        prepareSelection();
        QTextBrowser::inputMethodEvent(event);
        if(sourceExposed&&(!event->commitString().isEmpty()||event->replacementLength()))
            rebuild(-1,0,sourcePosition(textCursor().position()));
    }

std::optional<QPair<int,int>> LiveEditor::listPrefixAt(int at)const{
        const int start=at==0?0:source.lastIndexOf('\n',at-1)+1;
        const auto marker=listMarker().match(source.mid(start).section('\n',0,0));
        if(!marker.hasMatch())return {};
        const int end=start+marker.capturedLength();
        if(sourceExposed){
            for(const auto &span:scanSpans(source))
                if(span.kind==SpanKind::Code&&span.start<=start&&span.start+span.length>start)return {};
            return qMakePair(start,end);
        }
        for(const auto &region:regions)
            if(region.sourceStart<=end&&region.sourceEnd>=start&&!region.codeBlock&&
                (region.listItem||document()->findBlock(region.start).textList()))return qMakePair(start,end);
        return {};
    }

QTextCharFormat LiveEditor::codeCharacters()const{
        auto codeFont=font();
        codeFont.setFamily(QString::fromLatin1(editorFont));
        codeFont.setStyleHint(QFont::Monospace);
        QTextCharFormat characters;
        characters.setFont(codeFont);
        characters.setForeground(QColor("#37474f"));
        return characters;
    }

bool LiveEditor::viewportEvent(QEvent *event){
    if(event->type()==QEvent::Resize||event->type()==QEvent::Show){
        QTimer::singleShot(0,this,[this]{if(!rebuilding)layoutNativeTables();});
    }
    if(event->type()==QEvent::Leave){tableHoverPoint=QPoint(-100,-100);viewport()->update();}
    return QTextBrowser::viewportEvent(event);
}

void LiveEditor::paintEvent(QPaintEvent *event){
        syncPdfViews();
        // Paint one panel per source region, including the leading, trailing
        // and inter-line space that paragraph backgrounds leave uncovered.
        {
            QPainter painter(viewport());
            painter.setClipRegion(event->region());
            auto *layout=document()->documentLayout();
            const QPointF scroll(horizontalScrollBar()->value(),verticalScrollBar()->value());
            for(const auto &region:regions){
                if(!region.codeBlock)continue;
                const auto first=document()->findBlock(region.start);
                const auto last=document()->findBlock(region.end);
                if(!first.isValid()||!last.isValid())continue;
                const auto top=layout->blockBoundingRect(first);
                const auto bottom=layout->blockBoundingRect(last);
                QRectF panel(top.left(),top.top(),top.width(),bottom.bottom()-top.top());
                panel.translate(-scroll);
                if(panel.intersects(event->rect()))painter.fillRect(panel,codeBlockBackground);
            }
        }
        NumberedEditor<QTextBrowser>::paintEvent(event);
        QPainter controls(viewport());controls.setRenderHint(QPainter::Antialiasing);
        for(const auto &item:axisControls()){
            controls.setPen(Qt::NoPen);
            if(axisSource==item.start&&axisIndex>=0){const auto &rects=axisRow?item.rows:item.columns;
                if(axisIndex<rects.size()){
                    controls.save();
                    controls.setBrush(QColor(137,102,194,40));
                    controls.drawRect(rects[axisIndex]);
                    controls.setBrush(Qt::NoBrush);controls.setPen(QPen(QColor("#8856bc"),2));
                    controls.drawRect(rects[axisIndex]);
                    controls.restore();
                }}
            auto grip=[&](QRectF rect,bool row){controls.setPen(Qt::NoPen);controls.setBrush(QColor("#eee9f7"));controls.drawRoundedRect(rect,3,3);controls.setBrush(QColor("#80708f"));
                for(int i=0;i<3;++i)controls.drawEllipse(row?QPointF(rect.center().x(),rect.top()+3+i*4):QPointF(rect.left()+3+i*4,rect.center().y()),1,1);};
            for(int i=0;i<item.rows.size();++i){const auto &rect=item.rows[i];
                if(QRectF(item.frame.left()-22,rect.top()-2,23,rect.height()+4).contains(tableHoverPoint)||(axisPressed&&axisSource==item.start&&axisRow&&axisIndex==i))
                    grip(QRectF(item.frame.left()-19,rect.center().y()-7,15,14),true);
            }
            for(int i=0;i<item.columns.size();++i){const auto &rect=item.columns[i];
                if(QRectF(rect.left()-2,item.frame.top()-22,rect.width()+4,23).contains(tableHoverPoint)||(axisPressed&&axisSource==item.start&&!axisRow&&axisIndex==i))
                    grip(QRectF(rect.center().x()-7,item.frame.top()-19,14,15),false);
            }
            auto plus=[&](QRectF rect){if(!rect.adjusted(-3,-3,3,3).contains(tableHoverPoint))return;
                controls.setPen(Qt::NoPen);controls.setBrush(QColor("#eee9f7"));controls.drawRoundedRect(rect,4,4);
                controls.setPen(QPen(QColor("#79529d"),2));const QPointF center=rect.center();controls.drawLine(center+QPointF(-4,0),center+QPointF(4,0));controls.drawLine(center+QPointF(0,-4),center+QPointF(0,4));};
            plus(item.addRow);plus(item.addColumn);
            if(axisPressed&&axisStarted&&axisBoundary>=0&&axisSource==item.start){controls.setPen(QPen(QColor("#8856bc"),3));
                if(axisRow)controls.drawLine(QPointF(item.frame.left(),axisDropPosition),QPointF(item.frame.right(),axisDropPosition));
                else controls.drawLine(QPointF(axisDropPosition,item.frame.top()),QPointF(axisDropPosition,item.frame.bottom()));}
        }

        for(const auto &item:codeCopyControls()){
            const bool copied=copiedCodeSource==regions[item.region].sourceStart;
            const bool hovered=item.rect.contains(tableHoverPoint);
            controls.save();
            controls.setPen(Qt::NoPen);
            controls.setBrush(QColor(copied?"#e2efe7":hovered?"#ddd8e5":"#efedf1"));
            controls.drawRoundedRect(item.rect,6,6);
            // Draw in logical coordinates so the icon stays crisp on Retina displays.
            controls.translate(item.rect.center()-QPointF(8,8));
            controls.setBrush(Qt::NoBrush);
            controls.setPen(QPen(QColor(copied?"#3c8060":hovered?"#655080":"#82798d"),
                                 1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            if(copied){
                QPainterPath check;check.moveTo(3,8);check.lineTo(6.5,11.5);check.lineTo(13,4.5);
                controls.drawPath(check);
            }else{
                QPainterPath back;back.moveTo(5,10);back.lineTo(3.5,10);
                back.quadTo(2,10,2,8.5);back.lineTo(2,3.5);back.quadTo(2,2,3.5,2);
                back.lineTo(8.5,2);back.quadTo(10,2,10,3.5);back.lineTo(10,5);
                controls.drawPath(back);
                controls.drawRoundedRect(QRectF(6,6,8,8),1.5,1.5);
            }
            controls.restore();
        }
        for(const auto &item:blockControls()){
            auto button=[&](const QRectF &rect,bool open,bool enabled){
                const bool hovered=enabled&&rect.contains(tableHoverPoint);
                controls.save();
                controls.setPen(Qt::NoPen);
                controls.setBrush(QColor(hovered?"#ddd8e5":"#efedf1"));
                controls.drawRoundedRect(rect,6,6);
                controls.translate(rect.center()-QPointF(8,8));
                controls.setBrush(Qt::NoBrush);
                controls.setPen(QPen(QColor(!enabled?"#aaa3b2":hovered?"#655080":"#82798d"),
                                     1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                QPainterPath icon;
                if(open){
                    // Open the original file in its external application.
                    icon.moveTo(7,3);icon.lineTo(3,3);icon.lineTo(3,13);
                    icon.lineTo(13,13);icon.lineTo(13,9);
                    icon.moveTo(9,2);icon.lineTo(14,2);icon.lineTo(14,7);
                    icon.moveTo(14,2);icon.lineTo(7,9);
                }else{
                    // Source brackets and slash identify Markdown source editing.
                    icon.moveTo(5,4);icon.lineTo(1,8);icon.lineTo(5,12);
                    icon.moveTo(11,4);icon.lineTo(15,8);icon.lineTo(11,12);
                    icon.moveTo(9.5,2.5);icon.lineTo(6.5,13.5);
                }
                controls.drawPath(icon);
                controls.restore();
            };
            button(item.open,true,!item.file.isEmpty());button(item.edit,false,true);
        }
    }

QTextBlockFormat LiveEditor::codeBlockFormat()const{
        QTextBlockFormat format;
        setBodyLineHeight(format,font());
        format.setBackground(codeBlockBackground);
        return format;
    }

QTextCharFormat LiveEditor::headingStyle(int level,const QFont &font,QTextBlockFormat &format){
        QTextDocument reference;
        reference.setDefaultFont(font);
        reference.setMarkdown(QString(level,'#')+" Heading");
        const auto heading=reference.begin();
        format=heading.blockFormat();
        const auto characters=headingCharacters(level,font);
        format.setLineHeight(qCeil(QFontMetricsF(characters.font()).lineSpacing()),QTextBlockFormat::FixedHeight);
        return characters;
    }

bool LiveEditor::isStandaloneImage(const QTextBlock &block){
        if(block.text().trimmed()!=QString(QChar::ObjectReplacementCharacter))return false;
        for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(format.isImageFormat())return !format.hasProperty(formulaPaintAscent);
        }
        return false;
    }

void LiveEditor::fitInlineImages(const QTextBlock &block,QTextDocument *document){
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
            if(QRegularExpression(R"(\|[1-9][0-9]*$)").match(image.stringProperty(QTextFormat::ImageAltText)).hasMatch())continue;
            const QSizeF size=handler->intrinsicSize(document,position,image);
            if(size.height()<=ascent)continue;
            image.setWidth(size.width()*ascent/size.height());
            image.setHeight(ascent);
            cursor.setCharFormat(image);
        }
    }

int LiveEditor::regionAt(int pos)const{
        // At an inline boundary the following span owns the caret.
        for(int i=0;i<regions.size();++i){
            const auto &r=regions[i];
            if(pos<r.start||pos>r.end)continue;
            if(pos==r.end&&i+1<regions.size()&&regions[i+1].start==pos)continue;
            return i;
        }
        return -1;
    }

void LiveEditor::activateCaretRegion(){
        const auto caret=textCursor();
        if(caret.hasSelection())return;
        if(caret.currentTable()&&bindNativeTable(caret))return;
        if(nativeTable)revealNativeCell(caret);
        nativeTable.clear();nativeStart=nativeEnd=-1;
        const int i=regionAt(textCursor().position());
        if(isResourceRegion(i)&&regions[i].sourceStart!=resourceSourceEditStart)return;
        if(!isResourceRegion(i))resourceSourceEditStart=-1;
        if(i>=0&&i!=active)rebuild(i,sourcePosition(textCursor().position())-regions[i].sourceStart);
    }

bool LiveEditor::isResourceRegion(int index) const{
        if(index<0||index>=regions.size()||regions[index].codeBlock||regions[index].inlinePart)return false;
        const auto &region=regions[index];
        const auto nodes=mdedit::MarkdownParser::parse(source.mid(region.sourceStart,region.sourceEnd-region.sourceStart));
        return nodes.size()==1&&nodes.first().resource.has_value();
    }

QList<QPair<int,bool>> LiveEditor::inlinePieces(const QString &text){
        // Use independent inline spans only where they don't change the
        // surrounding Markdown structure. Complex blocks use the full parser.
        if(text.startsWith(' ')||text.startsWith('\t'))return {};
        static const QRegularExpression blockMarker(R"((?:^|\n)(?: {0,3}(?:#{1,6}(?: |$)|>|[-+*](?: |$)|\d+[.)] )| {4}|\t))");
        if(blockMarker.match(text).hasMatch())return {};

        if(text.contains('\n')&&!isMultilineParagraph(text))return {};
        // Nested styling needs its surrounding delimiters while editing.
        // Splitting out only the inner token would lose the outer style.
        QList<QPair<int,int>> styledRanges;
        for(const auto &style:inlineEditingStyles(text))
            if(style.format.hasProperty(QTextFormat::FontWeight)||style.format.hasProperty(QTextFormat::FontItalic))
                styledRanges.append({style.start,style.start+style.length});
        std::sort(styledRanges.begin(),styledRanges.end());
        for(int i=1;i<styledRanges.size();++i)
            if(styledRanges[i].first<styledRanges[i-1].second)return {};
        QList<QPair<int,int>> tokens;
        for(const auto &span:scanSpans(text)){
            if(span.kind==SpanKind::DisplayMath)return {};
            if(span.kind==SpanKind::Code&&text.mid(span.start,span.length).contains('\n'))return {};
            tokens.append({span.start,span.start+span.length});
        }
        for(const auto &range:highlightRanges(text))tokens.append(range);
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
        const auto literalGap=[&](const QString &gap){
            if(!otherMarkup.match(gap.trimmed()).hasMatch())return true;
            // Incomplete delimiters are ordinary text. They must not force
            // unrelated, valid inline spans into the source fallback.
            QTextDocument reference;reference.setMarkdown(gap.trimmed());
            return reference.toPlainText()==gap.trimmed();
        };
        QList<QPair<int,bool>> pieces;
        int at=0;
        for(const auto &token:tokens){
            if(token.first<at)return {};
            if(token.first>at){
                if(!literalGap(text.mid(at,token.first-at)))return {};
                pieces.append({token.first,false});
            }
            pieces.append({token.second,true});
            at=token.second;
        }
        if(at<text.size()){
            if(!literalGap(text.mid(at)))return {};
            pieces.append({text.size(),false});
        }
        return pieces;
    }

bool LiveEditor::isTextBlock(const QTextBlock &block){
        const auto format=block.blockFormat();
        return !block.textList()&&!QTextCursor(block).currentTable()&&!format.indent()&&
            !isCodeBlock(format)&&
            !format.hasProperty(QTextFormat::BlockQuoteLevel)&&!format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    }

bool LiveEditor::isMultilineParagraph(const QString &text){
        if(!text.contains('\n'))return false;
        for(const auto &span:scanSpans(text))
            if(span.kind==SpanKind::DisplayMath||text.mid(span.start,span.length).contains('\n'))return false;
        // A terminal backslash is already a Markdown hard break.
        if(text.contains("\\\n"))return false;
        QTextDocument reference;reference.setMarkdown(text);
        const auto block=reference.begin();
        return reference.blockCount()==1&&isTextBlock(block)&&!block.blockFormat().headingLevel();
    }

void LiveEditor::preserveResourceSlot(){
        if(resourceEditSlotHeight<=0||!isResourceRegion(active)||!document()->isLayoutEnabled())return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker signalBlocker(this);
        auto block=document()->findBlock(regions[active].start);
        const qreal top=document()->documentLayout()->blockBoundingRect(block).top();
        const qreal bottom=block.next().isValid()?document()->documentLayout()->blockBoundingRect(block.next()).top():document()->size().height()-document()->documentMargin();
        QTextCursor cursor(block);auto format=block.blockFormat();
        format.setBottomMargin(format.bottomMargin()+qMax(qreal(0),resourceEditSlotHeight-(bottom-top)));cursor.setBlockFormat(format);
    }

void LiveEditor::styleActiveSource(){
        if(active<0)return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker blockEditor(this);
        const auto &region=regions[active];
        const QString raw=source.mid(region.sourceStart,region.end-region.start);
        const auto body=codeBody(raw,region.codeFenceStart);
        const bool fenced=body.start>0;
        const bool joinedParagraph=!region.codeBlock&&!fenced&&isMultilineParagraph(raw);
        QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
        static const QRegularExpression heading("^ {0,3}(#{1,6})(?:[ \t]+|$)");
        static const QRegularExpression quote("^ {0,3}>");
        if(region.inlinePart){
            QTextCursor part(document());part.setPosition(region.start);
            part.setPosition(region.end,QTextCursor::KeepAnchor);
            QTextCharFormat normal;normal.setFont(font());part.setCharFormat(normal);
        }
        for(auto block=document()->findBlock(region.start);!region.inlinePart&&block.isValid()&&block.position()<=region.end;block=block.next()){
            QTextCharFormat characters;characters.setFont(font());
            QTextBlockFormat paragraph=bodyStyle.begin().blockFormat();
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
            setBodyLineHeight(paragraph,font());
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
        if(!region.codeBlock&&!fenced)for(const auto &style:inlineEditingStyles(raw)){
            QTextCursor token(document());token.setPosition(region.start+style.start);
            token.setPosition(token.position()+style.length,QTextCursor::KeepAnchor);
            token.mergeCharFormat(style.format);
        }
        if(region.codeBlock||fenced)colorCode(raw.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
            QTextCursor token(document());token.setPosition(region.start+body.start+start);
            token.setPosition(token.position()+length,QTextCursor::KeepAnchor);
            QTextCharFormat format;format.setForeground(color);token.mergeCharFormat(format);
        });
    }

int LiveEditor::sourcePosition(int pos)const{
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

void LiveEditor::exposeSource(int begin,int end){
        rebuilding=true;setPlainText(source);
        sourceExposed=true;
        regions={{0,int(source.size()),0,int(source.size())}};active=0;oldText=source;
        QTextCursor selected(document());selected.setPosition(qBound(0,begin,int(source.size())));
        selected.setPosition(qBound(0,end,int(source.size())),QTextCursor::KeepAnchor);
        setTextCursor(selected);styleActiveSource();rebuilding=false;
    }

void LiveEditor::prepareSelection(){
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1){
            exposeSource(caret.anchor()<caret.position()?0:int(source.size()),
                         caret.anchor()<caret.position()?int(source.size()):0);return;
        }
        if(active<0||caret.selectionStart()<regions[active].start||caret.selectionEnd()>regions[active].end)
            exposeSource(sourcePosition(caret.anchor()),sourcePosition(caret.position()));
    }

const QTextDocument *LiveEditor::renderedDocument(const QString &text,bool joinedParagraph,bool inlinePart){
        // Inline pieces and paragraphs use different base URLs and block formatting.
        const QString key=(inlinePart?"i":joinedParagraph?"j":"p")+text;
        if(auto *cached=renderedMarkdown.object(key))return cached;
        if(text.contains("!["))resourceRenderKeys.insert(key);
        auto *rendered=new ResourceTextDocument(resourceManager);
        rendered->setDefaultFont(renderedFont);
        rendered->setBaseUrl(renderedBaseUrl);
        QStringList errors;
        const auto nodes=mdedit::MarkdownParser::parse(text);
        const auto resource=nodes.size()==1?nodes.first().resource:std::nullopt;
        const QUrl resolved=resource?mdedit::ResourceResolver::resolve(resource->uri,renderedBaseUrl):QUrl();
        const bool pdf=QFileInfo(resolved.path()).suffix().compare("pdf",Qt::CaseInsensitive)==0;
        const bool csv=QFileInfo(resolved.path()).suffix().compare("csv",Qt::CaseInsensitive)==0;
        if(!inlinePart&&resource&&!mdedit::ResourceResolver::isPreviewResource(resolved)){
            QUrl action("mdedit-pdf:/open");QUrlQuery query;
            query.addQueryItem("file",resolved.toString());action.setQuery(query);
            QTextCursor cursor(rendered);
            QTextBlockFormat block;block.setBackground(QColor("#f5f2f8"));
            block.setLineHeight(QFontMetricsF(font()).height()+12,QTextBlockFormat::FixedHeight);
            cursor.setBlockFormat(block);
            QTextCharFormat link;link.setAnchor(true);link.setAnchorHref(action.toString());
            link.setForeground(QColor("#655080"));link.setToolTip(QFileInfo(resolved.path()).fileName());
            const QString label=QStringLiteral("  📄  ")+QFileInfo(resolved.path()).fileName();
            cursor.insertText(QFontMetricsF(font()).elidedText(label,Qt::ElideMiddle,qMax(40,viewport()->width()-40)),link);
        }else if(!inlinePart&&resource){
            const QString resourceKey=resourceManager.request(resolved);
            const auto *snapshot=resourceManager.lookup(resourceKey);
            const QString state=!snapshot?"loading":snapshot->error.isEmpty()?"ready":"error";
            const bool textResource=mdedit::ResourceResolver::isTextResource(resolved);
            const bool picture=!pdf&&!csv&&!textResource;
            int width=qBound(320,viewport()->width()-80,960);
            int height=pdf?480:8*24+70;
            if(picture){
                const QSize size=snapshot&&!snapshot->image.isNull()?snapshot->image.size():QSize(400,300);
                width=resource->width>0?resource->width:qMin(size.width(),width);
                height=qBound(1.0,double(size.height())*width/size.width(),double(std::numeric_limits<int>::max()/4));
            } // Loading and ready snapshots share geometry.
            const QSize raster=QSize(width,height).scaled(1920,1920,Qt::KeepAspectRatio);
            QImage image(raster,QImage::Format_ARGB32_Premultiplied);image.fill(picture?Qt::transparent:QColor("#f5f2f8"));
            QPainter painter(&image);painter.scale(double(raster.width())/width,double(raster.height())/height);painter.setFont(font());painter.setPen(QColor("#524960"));
            painter.save();
            QFont titleFont=font();titleFont.setBold(true);painter.setFont(titleFont);
            const QString title=resource->alt.trimmed().isEmpty()?(pdf?QStringLiteral("PDF 文档"):csv?QStringLiteral("CSV 表格"):QFileInfo(resolved.path()).fileName()):resource->alt;
            if(!picture)painter.drawText(QRectF(12,8,width-24,28),Qt::AlignVCenter|Qt::TextSingleLine,
                QFontMetricsF(titleFont).elidedText(title,Qt::ElideRight,width-24));
            painter.restore();
            const QRectF content=picture?QRectF(0,0,width,height):QRectF(12,42,width-24,height-54);
            if(snapshot)mdedit::RendererRegistry::paint(painter,*snapshot,content);
            else painter.drawText(content,Qt::AlignCenter,pdf?"PDF 正在加载…":csv?"CSV 正在加载…":textResource?"文本正在加载…":"图片正在加载…");
            painter.end();
            const QUrl imageUrl((pdf?"pdf-":csv?"csv-":textResource?"text-":"image-")+state+":/"+QString::number(qHash(resourceKey)));
            rendered->addResource(QTextDocument::ImageResource,imageUrl,image);
            QTextImageFormat format;format.setName(imageUrl.toString());format.setWidth(width);format.setHeight(height);
            if(pdf)format.setProperty(QTextFormat::UserProperty+82,resolved);
            QTextCursor cursor(rendered);cursor.insertImage(format);
            if(textResource){
                cursor.insertBlock();
                auto link=[&](const QString &label,const QString &command){
                    QUrl action("mdedit-pdf:/"+command);QUrlQuery query;query.addQueryItem("file",resolved.toString());action.setQuery(query);
                    QTextCharFormat f;f.setAnchor(true);f.setAnchorHref(action.toString());f.setForeground(QColor("#655080"));f.setFontUnderline(true);
                    cursor.insertText(label,f);cursor.insertText("   ",QTextCharFormat());
                };


            }
        }else renderMarkdown(rendered,text,errors,true);
        QSet<QTextTable *> sizedTables;
        for(auto block=rendered->begin();block.isValid();block=block.next()){
            if(auto *table=QTextCursor(block).currentTable();table&&!sizedTables.contains(table)){
                sizedTables.insert(table);
                // Qt imports consecutive empty Markdown cells as a colspan.
                // Native Markdown tables have one independent cell per column.
                if(nodes.size()==1&&nodes.first().kind==mdedit::BlockKind::Table)
                    for(int row=0;row<table->rows();++row)for(int column=0;column<table->columns();++column){
                        const auto cell=table->cellAt(row,column);
                        if(cell.rowSpan()>1||cell.columnSpan()>1)table->splitCell(row,column,1,1);
                    }
                layoutNativeTable(table);
            }
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();auto image=fragment.charFormat().toImageFormat();
                if(!image.isValid()||image.hasProperty(formulaPaintAscent))continue;
                const auto match=QRegularExpression(R"(\|([1-9][0-9]*)$)").match(image.stringProperty(QTextFormat::ImageAltText));
                bool valid=false;const int width=match.captured(1).toInt(&valid);
                if(!valid)continue;
                const QImage pixels=qvariant_cast<QImage>(rendered->resource(QTextDocument::ImageResource,QUrl(image.name())));
                image.setWidth(width);
                if(!pixels.isNull())image.setHeight(double(pixels.height())*width/pixels.width());
                QTextCursor cursor(rendered);cursor.setPosition(fragment.position());cursor.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);cursor.setCharFormat(image);
            }
            auto format=block.blockFormat();
            if(QTextCursor(block).currentTable())format.setAlignment(Qt::AlignLeft);
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

const QRegularExpression &LiveEditor::listMarker(){
        static const QRegularExpression marker(R"(^[ \t]*(?:[-+*]|[0-9]{1,9}[.)])(?:[ \t]+|$)(?:\[[ xX]\][ \t]+)?)");
        return marker;
    }

void LiveEditor::handlePdfAction(const QUrl &action){
    const QUrl file(QUrlQuery(action).queryItemValue("file",QUrl::FullyDecoded));
    if(!file.isLocalFile())return;
    if(action.path()=="/open"){QDesktopServices::openUrl(file);return;}
}

bool LiveEditor::editResourceSourceAt(int sourceOffset){
    for(int i=0;i<regions.size();++i){
        if(sourceOffset>=regions[i].sourceStart&&sourceOffset<regions[i].sourceEnd&&isResourceRegion(i)){
            resourceSourceEditStart=regions[i].sourceStart;rebuild(i,0);return true;
        }
    }
    return false;
}

void LiveEditor::syncPdfViews(){
    QSet<QString> used;
    if(!sourceExposed)for(auto block=document()->begin();block.isValid();block=block.next()){
        const auto bounds=document()->documentLayout()->blockBoundingRect(block).translated(-horizontalScrollBar()->value(),-verticalScrollBar()->value());
        for(auto it=block.begin();!it.atEnd();++it){
            const auto fragment=it.fragment();const auto format=fragment.charFormat();
            if(!format.isImageFormat()||!format.hasProperty(QTextFormat::UserProperty+82))continue;
            const auto image=format.toImageFormat();const QUrl file=format.property(QTextFormat::UserProperty+82).toUrl();
            const QString key=QString::number(sourcePosition(fragment.position()))+"|"+file.toString();
            used.insert(key);
            QTextCursor cursor(document());cursor.setPosition(fragment.position());
            const QRect geometry=QRectF(cursorRect(cursor).left()+12,bounds.top()+42,image.width()-24,image.height()-54).toAlignedRect();
            auto *panel=pdfViews.value(key);
            if(!geometry.intersects(viewport()->rect())){if(panel)panel->hide();continue;}
            if(!panel){
                const auto *snapshot=resourceManager.lookup(resourceManager.request(file));
                if(!snapshot||!snapshot->error.isEmpty()||!file.isLocalFile())continue;
                panel=new QWidget(viewport());panel->setObjectName("pdfPreviewPanel");panel->setAutoFillBackground(true);
                auto palette=panel->palette();palette.setColor(QPalette::Window,QColor("#f5f2f8"));panel->setPalette(palette);
                auto *layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
                auto *view=new NestedPdfView(this,panel);view->setObjectName("pdfViewer");
                auto *doc=new QPdfDocument(view);
                if(doc->load(file.toLocalFile())!=QPdfDocument::Error::None){delete panel;continue;}
                view->setDocument(doc);view->setPageMode(QPdfView::PageMode::MultiPage);view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
                view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
                auto *toolbar=new QHBoxLayout;
                auto *previous=new QPushButton("上一页",panel);auto *next=new QPushButton("下一页",panel);
                auto *page=new QSpinBox(panel);page->setObjectName("pdfPageNumber");page->setRange(1,doc->pageCount());page->setPrefix("第 ");page->setSuffix(QString(" / %1 页").arg(doc->pageCount()));
                toolbar->addWidget(previous);toolbar->addWidget(page);toolbar->addWidget(next);toolbar->addStretch();
                layout->addLayout(toolbar);layout->addWidget(view);
                connect(page,qOverload<int>(&QSpinBox::valueChanged),view,[view](int n){view->pageNavigator()->jump(n-1,QPointF());});
                connect(view->pageNavigator(),&QPdfPageNavigator::currentPageChanged,page,[page](int n){QSignalBlocker guard(page);page->setValue(n+1);});
                connect(previous,&QPushButton::clicked,page,[page]{page->setValue(page->value()-1);});
                connect(next,&QPushButton::clicked,page,[page]{page->setValue(page->value()+1);});
                pdfViews.insert(key,panel);
            }
            panel->setGeometry(geometry);panel->show();
        }
    }
    for(auto it=pdfViews.begin();it!=pdfViews.end();){
        if(!used.contains(it.key())){it.value()->deleteLater();it=pdfViews.erase(it);}else ++it;
    }
}
