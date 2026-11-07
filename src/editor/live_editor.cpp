#include "live_editor.h"
#include "../markdown/scanner.h"
#include "../markdown/renderer.h"

QString LiveEditor::clickableAnchorAt(const QPoint &point) const{
        const QString anchor=anchorAt(point);
        const QUrl url(anchor);
        if((url.isRelative()&&anchor.startsWith('#')&&!url.fragment().isEmpty())||
           ((url.scheme()=="http"||url.scheme()=="https")&&url.isValid()&&!url.host().isEmpty()))
            return anchor;
        return {};
    }

LiveEditor::LiveEditor(){
        document()->setIndentWidth(24);
        installFormulaImageHandler(document());
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

void LiveEditor::setSource(const QString &text){if(text==source&&!regions.isEmpty())return;source=text;rebuild(-1,0);}

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
        rebuilding=true;int scroll=verticalScrollBar()->value();
        // Intermediate document mutations must not repeatedly update the gutter.
        const QSignalBlocker blockEditor(this);
        sourceExposed=false;
        const int caretY=cursorRect().top();
        const int preferredX=movingVertically?textCursor().verticalMovementX():-1;
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
        styleActiveSource();
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));caret.setVerticalMovementX(preferredX);setTextCursor(caret);}
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);
        if(forcedTarget>=0)ensureCursorVisible();
        else if(target>=0)verticalScrollBar()->setValue(scroll+cursorRect().top()-caretY);
        rebuilding=false;refreshLineNumbers();
    }

void LiveEditor::mouseMoveEvent(QMouseEvent *event){
        QTextBrowser::mouseMoveEvent(event);
        if(event->buttons()==Qt::NoButton){
            const bool overLink=!clickableAnchorAt(event->position().toPoint()).isEmpty();
            viewport()->setCursor(overLink?Qt::PointingHandCursor:Qt::IBeamCursor);
        }
    }

void LiveEditor::mousePressEvent(QMouseEvent *event){
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

void LiveEditor::mouseReleaseEvent(QMouseEvent *event){
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

void LiveEditor::focusInEvent(QFocusEvent *event){
        QTextBrowser::focusInEvent(event);
        if(event->reason()!=Qt::ActiveWindowFocusReason&&active<0)activateCaretRegion();
    }

void LiveEditor::focusOutEvent(QFocusEvent *event){
        QTextBrowser::focusOutEvent(event);
        // Menus and dialogs take focus before their actions read the selection.
        // Rebuilding here discards it and moves the cursor to the document end.
        // The next caret movement will switch the active region when needed.
    }

void LiveEditor::keyPressEvent(QKeyEvent *event){
        QScopedValueRollback<bool> verticalNavigation(movingVertically,event->key()==Qt::Key_Up||event->key()==Qt::Key_Down);
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
        if((event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)&&
            !(event->modifiers()&~Qt::ShiftModifier)){
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

void LiveEditor::insertFromMimeData(const QMimeData *mime){
        prepareSelection();insertPlainText(mime->text());
        if(sourceExposed)rebuild(-1,0,sourcePosition(textCursor().position()));
    }

void LiveEditor::inputMethodEvent(QInputMethodEvent *event){
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

void LiveEditor::paintEvent(QPaintEvent *event){
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
                if(panel.intersects(event->rect()))painter.fillRect(panel,QColor("#f3f5f4"));
            }
        }
        NumberedEditor<QTextBrowser>::paintEvent(event);
    }

QTextBlockFormat LiveEditor::codeBlockFormat()const{
        QTextBlockFormat format;
        setBodyLineHeight(format,font());
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
        const int i=regionAt(caret.position());
        if(i>=0&&i!=active)rebuild(i,sourcePosition(caret.position())-regions[i].sourceStart);
    }

QList<QPair<int,bool>> LiveEditor::inlinePieces(const QString &text){
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

void LiveEditor::styleActiveSource(){
        if(active<0||regions[active].inlinePart)return;
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
        for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next()){
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

const QRegularExpression &LiveEditor::listMarker(){
        static const QRegularExpression marker(R"(^[ \t]*(?:[-+*]|[0-9]{1,9}[.)])(?:[ \t]+|$)(?:\[[ xX]\][ \t]+)?)");
        return marker;
    }
