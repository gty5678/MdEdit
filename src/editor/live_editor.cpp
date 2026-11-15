#include "live_editor.h"
#include "../markdown/renderer.h"

LiveEditor::LiveEditor(){
        copyFeedbackTimer.setSingleShot(true);
        copyFeedbackTimer.setInterval(3000);
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
        connect(this,&QTextEdit::selectionChanged,this,[this]{
            if(rebuilding||movingCursor||toPlainText()!=oldText)return;
            if(textCursor().hasSelection())exposeSelectedRegions();
            else if(selectionSourceExpanded)activateCaretRegion();
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

void LiveEditor::setSource(const QString &text){if(text==source&&!regions.isEmpty())return;cancelAxisDrag();cellRangeTable.clear();cellRangePressed=cellRangeDragging=false;axisSource=axisIndex=-1;resourceSourceEditStart=-1;mathEditSlotHeight=0;mathEditSourceStart=-1;source=text;rebuild(-1,0);}

QString LiveEditor::markdown()const{return source;}

void LiveEditor::refreshAppearance(){
        const auto range=sourceSelection();rebuild(-1,0);restoreSourceSelection(range.first,range.second);
    }
