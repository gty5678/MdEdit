#include "editor/markdown_editor_widget.h"
#include "editor/tables/table_editor_dialog.h"
#include "editor/tables/table_data.h"
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"Table edit failed "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
static bool waitFor(const std::function<bool()> &predicate){QElapsedTimer elapsed;elapsed.start();while(!predicate()&&elapsed.elapsed()<5000){QApplication::processEvents();QThread::msleep(1);}return predicate();}
static void click(QWidget *dialog,const char *name){dialog->findChild<QPushButton *>(name)->click();}
int runTableEditTests(){
    const QByteArray special="\xef\xbb\xbfname,value,notes\r\n\"a,b\",\"say \"\"hi\"\"\",\"two\nlines\"\r\n";
    const auto decoded=mdedit::readCsv(special);CHECK(decoded.error.isEmpty());CHECK(decoded.rows[1][2]=="two\nlines");
    CHECK(mdedit::readCsv(mdedit::writeCsv(decoded)).rows==decoded.rows);CHECK(mdedit::writeCsv(decoded).startsWith("\xef\xbb\xbf"));
    CHECK(!mdedit::readCsv("a,\"not closed").error.isEmpty());
    const QString original="Before.\n\n| Name | Value |\n| :--- | ---: |\n| alpha | 10 |\n| beta | 20 |\n\nAfter.\n";
    MarkdownEditorWidget widget;widget.resize(900,600);widget.setMarkdown(original);widget.show();
    auto *live=static_cast<LiveEditor *>(widget.findChild<QTextBrowser *>("markdownPreview"));CHECK(live);
    QApplication::processEvents();QTextCursor cell;
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){cell=QTextCursor(block);break;}
    CHECK(!cell.isNull());const QPoint tablePoint=live->cursorRect(cell).center();
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,tablePoint,live->viewport()->mapToGlobal(tablePoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(live->viewport(),&doubleClick);
    CHECK(!live->findChild<mdedit::TableEditorDialog *>());
    CHECK(live->textCursor().currentTable());
    auto *inlineTable=live->textCursor().currentTable();
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QKeyEvent typeCell(QEvent::KeyPress,0,Qt::NoModifier,QString::fromUtf8("新|值"));QApplication::sendEvent(live,&typeCell);
    CHECK(widget.markdown().contains(QString::fromUtf8("alpha新\\|值")));
    CHECK(live->textCursor().currentTable());CHECK(!widget.markdown().contains("<table"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(2,1).lastCursorPosition());
    QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(live,&tab);
    CHECK(live->textCursor().currentTable()->rows()==4);CHECK(live->textCursor().currentTable()->cellAt(live->textCursor()).row()==3);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // IME, multiline paste and formatting stay in the native table and source.
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QInputMethodEvent ime;ime.setCommitString(QString::fromUtf8("中文"));QApplication::sendEvent(live,&ime);
    CHECK(widget.markdown().contains(QString::fromUtf8("alpha中文")));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    auto selection=inlineTable->cellAt(1,0).firstCursorPosition();selection.setPosition(inlineTable->cellAt(1,0).lastPosition(),QTextCursor::KeepAnchor);live->setTextCursor(selection);
    widget.formatSelection("**","**");CHECK(widget.markdown().contains("**alpha**"));CHECK(live->textCursor().currentTable());
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).firstCursorPosition());
    widget.clipboardParagraph(false);CHECK(QApplication::clipboard()->text()=="alpha");
    QApplication::clipboard()->setText("two\nlines");
    QKeyEvent paste(QEvent::KeyPress,Qt::Key_V,Qt::ControlModifier);QApplication::sendEvent(live,&paste);
    CHECK(widget.markdown().contains("<br>"));CHECK(live->textCursor().currentTable());
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    const QString formulaSource="Before.\n\n| Formula | Other |\n| --- | --- |\n| $x^2$ | **keep** |\n\nAfter.\n";
    widget.setMarkdown(formulaSource);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QKeyEvent addText(QEvent::KeyPress,0,Qt::NoModifier," text");QApplication::sendEvent(live,&addText);
    CHECK(widget.markdown().contains("$x^2$"));CHECK(!widget.markdown().contains("math:/"));CHECK(widget.markdown().contains("**keep**"));
    CHECK(live->textCursor().block().text().contains("$x^2$"));
    live->setTextCursor(inlineTable->cellAt(1,1).firstCursorPosition());
    CHECK(live->textCursor().block().text()=="**keep**");
    CHECK(!inlineTable->cellAt(1,0).firstCursorPosition().block().text().contains("$x^2$"));
    CHECK(widget.markdown().contains("$x^2$"));
    auto rawBold=live->textCursor();rawBold.setPosition(inlineTable->cellAt(1,1).lastPosition(),QTextCursor::KeepAnchor);
    rawBold.insertText("**changed**");
    CHECK(widget.markdown().contains("**changed**"));
    live->setTextCursor(inlineTable->cellAt(1,0).firstCursorPosition());
    CHECK(inlineTable->cellAt(1,1).firstCursorPosition().block().text()=="changed");
    CHECK(live->textCursor().block().text().contains("$x^2$"));
    live->setTextCursor(QTextCursor(live->document()->lastBlock()));
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    CHECK(!inlineTable->cellAt(1,0).firstCursorPosition().block().text().contains("$x^2$"));
    CHECK(widget.markdown().contains("**changed**"));

    widget.setMarkdown(original);
    live->restoreSourceSelection(original.indexOf("alpha"),original.indexOf("alpha"));CHECK(live->textCursor().currentTable());
    inlineTable=live->textCursor().currentTable();live->setTextCursor(inlineTable->cellAt(0,0).firstCursorPosition());
    QKeyEvent backtab(QEvent::KeyPress,Qt::Key_Backtab,Qt::ShiftModifier);QApplication::sendEvent(live,&backtab);
    CHECK(!live->textCursor().currentTable());CHECK(widget.markdown()==original);
    live->restoreSourceSelection(original.indexOf("| Name"),original.indexOf("| Name"));CHECK(live->textCursor().currentTable());
    QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(live,&up);CHECK(!live->textCursor().currentTable());CHECK(widget.markdown()==original);
    CHECK(live->editTableAt(original.indexOf("| Name")));
    auto *dialog=live->findChild<mdedit::TableEditorDialog *>();CHECK(dialog);
    auto *grid=dialog->findChild<QTableWidget *>("tableCells");CHECK(grid->rowCount()==3&&grid->columnCount()==2);
    grid->setCurrentCell(1,0);click(dialog,"insertRowAfter");CHECK(grid->rowCount()==4);
    grid->setItem(2,0,new QTableWidgetItem("new | cell"));grid->setItem(2,1,new QTableWidgetItem("30"));
    click(dialog,"moveRowUp");CHECK(grid->item(1,0)->text()=="new | cell");
    click(dialog,"insertColumnAfter");CHECK(grid->columnCount()==3);
    grid->setItem(1,1,new QTableWidgetItem("middle"));click(dialog,"moveColumnRight");CHECK(grid->item(1,2)->text()=="middle");
    click(dialog,"removeColumn");CHECK(grid->columnCount()==2);
    grid->setCurrentCell(3,0);click(dialog,"removeRow");CHECK(grid->rowCount()==3);
    click(dialog,"applyTable");QApplication::processEvents();
    CHECK(widget.markdown().startsWith("Before.\n\n"));CHECK(widget.markdown().endsWith("\n\nAfter.\n"));
    CHECK(widget.markdown().contains("new \\| cell"));CHECK(widget.markdown().contains(":---"));CHECK(widget.document()->isModified());
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->moveTableBlockAt(original.indexOf("| Name"),-1));CHECK(widget.markdown().startsWith("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->moveTableBlockAt(original.indexOf("| Name"),1));CHECK(widget.markdown().indexOf("After.")<widget.markdown().indexOf("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Native Markdown menus operate on the cell that was right-clicked.
    widget.setMarkdown(original);QTextTable *native=nullptr;
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){native=QTextCursor(block).currentTable();break;}
    CHECK(native);const auto targetCell=native->cellAt(1,1).firstCursorPosition();const QPoint contextPoint=live->cursorRect(targetCell).center();
    QContextMenuEvent context(QContextMenuEvent::Mouse,contextPoint,live->viewport()->mapToGlobal(contextPoint));
    QApplication::sendEvent(live->viewport(),&context);
    auto *menu=live->findChild<QMenu *>("tableContextMenu");CHECK(menu);
    auto *insertRow=menu->findChild<QAction *>("insertRowAfter");CHECK(insertRow&&insertRow->isEnabled());insertRow->trigger();menu->close();
    auto nativeData=mdedit::readMarkdownTable(live->markdown().mid(live->markdown().indexOf("| Name"),live->markdown().indexOf("\n\nAfter")-live->markdown().indexOf("| Name")));
    CHECK(nativeData.rows.size()==4);CHECK(nativeData.rows[1][0]=="alpha");CHECK(nativeData.rows[2][0].isEmpty());CHECK(nativeData.rows[3][0]=="beta");
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"insertColumnBefore"));
    CHECK(widget.markdown().contains("| alpha |  | 10 |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"moveColumnLeft"));CHECK(widget.markdown().contains("| Value | Name |"));CHECK(widget.markdown().contains("| ---: | :--- |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,0,"moveRowDown"));CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,0,"removeRow"));CHECK(!widget.markdown().contains("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"removeColumn"));CHECK(widget.markdown().contains("| Name |"));CHECK(!widget.markdown().contains("Value"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    widget.setMarkdown(original);QApplication::processEvents();
    auto sendMouse=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons){
        QMouseEvent event(type,point,live->viewport()->mapToGlobal(point),button,buttons,Qt::NoModifier);QApplication::sendEvent(live->viewport(),&event);
    };
    CHECK(live->moveMarkdownTableTo(original.indexOf("| Name"),0));CHECK(widget.markdown().startsWith("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(!live->moveMarkdownTableTo(original.indexOf("| Name"),1));CHECK(widget.markdown()==original);
    // Native row/column grips reorder in place; edge buttons append axes.
    widget.setMarkdown(original);QApplication::processEvents();
    auto geometry=[&]{QTextTable *table=nullptr;for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){table=QTextCursor(block).currentTable();break;}return table;};
    auto rowPoint=[&](int row){auto *table=geometry();const auto frame=live->document()->documentLayout()->frameBoundingRect(table);const auto rect=live->document()->documentLayout()->blockBoundingRect(table->cellAt(row,0).firstCursorPosition().block());return QPoint(qRound(frame.left()-12)-live->horizontalScrollBar()->value(),qRound(rect.center().y())-live->verticalScrollBar()->value());};
    auto colPoint=[&](int column){auto *table=geometry();const auto frame=live->document()->documentLayout()->frameBoundingRect(table);const auto rect=live->document()->documentLayout()->blockBoundingRect(table->cellAt(0,column).firstCursorPosition().block());return QPoint(qRound(rect.center().x())-live->horizontalScrollBar()->value(),qRound(frame.top()-12)-live->verticalScrollBar()->value());};
    const QPoint hover=rowPoint(1),interior=colPoint(0)+QPoint(0,40);
    sendMouse(QEvent::MouseMove,interior,Qt::NoButton,Qt::NoButton);CHECK(live->viewport()->cursor().shape()!=Qt::OpenHandCursor);
    auto gripColor=[&]{auto image=live->viewport()->grab().toImage();const qreal scale=image.devicePixelRatio();return image.pixelColor(qRound(hover.x()*scale),qRound(hover.y()*scale));};
    const QColor hidden=gripColor();sendMouse(QEvent::MouseMove,hover,Qt::NoButton,Qt::NoButton);CHECK(live->viewport()->cursor().shape()==Qt::OpenHandCursor);CHECK(gripColor()!=hidden);
    sendMouse(QEvent::MouseMove,interior,Qt::NoButton,Qt::NoButton);CHECK(gripColor()==hidden);
    const QImage nativeGrid=live->viewport()->grab().toImage();
    QPoint select=rowPoint(1);sendMouse(QEvent::MouseButtonPress,select,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,select,Qt::LeftButton,Qt::NoButton);
    // Compare the painted selection edges to the native grid, rather than
    // relying on frameBoundingRect (which includes a first-cell offset).
    const QImage selectedRow=live->viewport()->grab().toImage();
    CHECK(!selectedRow.isNull());
    auto horizontalEdges=[&](const QImage &image,bool selection){
        QList<int> edges;const qreal scale=image.devicePixelRatio();
        const auto frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
        const int left=qMax(0,qRound((frame.left()-12)*scale)),right=qMin(image.width()-1,qRound((frame.right()+12)*scale));
        for(int y=qMax(0,qRound((frame.top()-16)*scale));y<qMin(image.height(),qRound(frame.bottom()*scale));++y){
            int count=0;
            for(int x=left;x<=right;++x){const auto color=image.pixelColor(x,y);
                if(selection?color.red()>90&&color.red()<180&&color.blue()>140&&color.blue()>color.red()+20&&color.green()<150:
                    qAbs(color.red()-color.green())<3&&qAbs(color.green()-color.blue())<3&&color.red()>90&&color.red()<230)++count;
            }
            if(count>(right-left-24*scale)*0.8)edges.append(y);
        }return edges;
    };
    const auto gridEdges=horizontalEdges(nativeGrid,false),selectionEdges=horizontalEdges(selectedRow,true);
    CHECK(!selectionEdges.isEmpty());
    for(int edge:selectionEdges){bool aligned=false;for(int grid:gridEdges)aligned|=qAbs(edge-grid)<=2*selectedRow.devicePixelRatio();CHECK(aligned);}

    widget.clipboardParagraph(false);CHECK(QApplication::clipboard()->text()=="alpha\t10");
    QKeyEvent deleteRow(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(live,&deleteRow);CHECK(!widget.markdown().contains("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    QPoint start=rowPoint(1),target=rowPoint(2);target.setY(target.y()+10);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("alpha"));widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    start=colPoint(1);target=colPoint(0);target.setX(target.x()-30);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().contains("| Value | Name |"));CHECK(widget.markdown().contains("| ---: | :--- |"));widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    auto frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
    QPoint plus(qRound(frame.center().x()),qRound(frame.bottom()+10));sendMouse(QEvent::MouseMove,plus,Qt::NoButton,Qt::NoButton);sendMouse(QEvent::MouseButtonPress,plus,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,plus,Qt::LeftButton,Qt::NoButton);
    CHECK(geometry()->rows()==4);widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
    plus=QPoint(qRound(frame.right()+10),qRound(frame.center().y()));sendMouse(QEvent::MouseMove,plus,Qt::NoButton,Qt::NoButton);sendMouse(QEvent::MouseButtonPress,plus,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,plus,Qt::LeftButton,Qt::NoButton);
    CHECK(geometry()->columns()==3);
    const auto emptyColumn=live->document()->documentLayout()->blockBoundingRect(geometry()->cellAt(0,2).firstCursorPosition().block());
    CHECK(emptyColumn.width()>=70);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    {
        LiveEditor responsive;responsive.resize(640,500);responsive.show();responsive.setSource(original);QApplication::processEvents();
        auto table=[&]()->QTextTable *{for(auto block=responsive.document()->begin();block.isValid();block=block.next())if(auto *grid=QTextCursor(block).currentTable())return grid;return nullptr;};
        CHECK(table());
        auto width=[&]{return responsive.document()->documentLayout()->frameBoundingRect(table()).width();};
        auto cellWidth=[&](int column){return responsive.document()->documentLayout()->blockBoundingRect(table()->cellAt(0,column).firstCursorPosition().block()).width();};
        CHECK(cellWidth(0)>=65);CHECK(qAbs(cellWidth(0)-cellWidth(1))<2);
        for(auto block=responsive.document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable())CHECK(block.blockFormat().alignment()==Qt::AlignLeft);
        CHECK(responsive.markdown()==original);
        const qreal initial=width();
        responsive.setTextCursor(table()->cellAt(1,0).lastCursorPosition());
        QInputMethodEvent composing(QStringLiteral("中文输入候选文字"),{});QApplication::sendEvent(&responsive,&composing);QApplication::processEvents();
        CHECK(responsive.markdown()==original);CHECK(width()>initial);
        CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        QInputMethodEvent cancelComposition;QApplication::sendEvent(&responsive,&cancelComposition);QApplication::processEvents();
        CHECK(responsive.markdown()==original);CHECK(qAbs(width()-initial)<2);
        QInputMethodEvent composingAgain(QStringLiteral("中文输入候选文字"),{});QApplication::sendEvent(&responsive,&composingAgain);
        const qreal composingWidth=width();
        QInputMethodEvent commitComposition;commitComposition.setCommitString(QStringLiteral("中文输入候选文字"));QApplication::sendEvent(&responsive,&commitComposition);QApplication::processEvents();
        CHECK(responsive.markdown().contains(QStringLiteral("alpha中文输入候选文字")));
        CHECK(qAbs(width()-composingWidth)<3);CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        responsive.setSource(original);QApplication::processEvents();responsive.setTextCursor(table()->cellAt(1,0).lastCursorPosition());
        QKeyEvent grow(QEvent::KeyPress,0,Qt::NoModifier,"abcdefghijklm");QApplication::sendEvent(&responsive,&grow);QApplication::processEvents();
        CHECK(width()>initial);CHECK(cellWidth(0)>cellWidth(1)+20);CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        QKeyEvent overflow(QEvent::KeyPress,0,Qt::NoModifier,QString(200,'W'));QApplication::sendEvent(&responsive,&overflow);QApplication::processEvents();
        CHECK(width()<=responsive.viewport()->width()-2*responsive.document()->documentMargin()+20);
        CHECK(cellWidth(0)>cellWidth(1)+20);
        CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()>1);
        CHECK(responsive.markdown().contains("| :--- | ---: |"));
        const qreal wide=width();const QString beforeResize=responsive.markdown();
        responsive.resize(400,500);QApplication::processEvents();QApplication::processEvents();
        CHECK(width()<wide);CHECK(responsive.markdown()==beforeResize);
    }
    {
        LiveEditor separated;separated.resize(640,600);separated.show();
        const QString two="| A | B |\n| --- | --- |\n| one | two |\n\n| C | D |\n| --- | --- |\n| three | four |\n";
        separated.setSource(two);QApplication::processEvents();
        QList<QTextTable *> tables;
        for(auto block=separated.document()->begin();block.isValid();block=block.next())if(auto *table=QTextCursor(block).currentTable();table&&!tables.contains(table))tables.append(table);
        CHECK(tables.size()==2);
        const auto first=separated.document()->documentLayout()->frameBoundingRect(tables[0]);
        const auto second=separated.document()->documentLayout()->frameBoundingRect(tables[1]);
        CHECK(second.top()-first.bottom()>=16);CHECK(separated.markdown()==two);
    }
    {
        LiveEditor emptyCells;emptyCells.resize(900,600);emptyCells.show();
        const QString source="| 负责人 | 状态 |  | **的三啊** |\n| --- | --- | --- | --- |\n| 李四 | 待验证 |  |  |\n| 张三 | 待验证 |  | 内容 |\n| 王五 | 待验证 | 待验证 | 待验证 |\n|  |  |  |  |\n";
        emptyCells.setSource(source);QApplication::processEvents();
        QTextTable *table=nullptr;
        for(auto block=emptyCells.document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){table=QTextCursor(block).currentTable();break;}
        CHECK(table);CHECK(table->rows()==5);CHECK(table->columns()==4);
        for(int row=0;row<5;++row)for(int column=0;column<4;++column){
            const auto cell=table->cellAt(row,column);
            CHECK(cell.row()==row);CHECK(cell.column()==column);CHECK(cell.rowSpan()==1);CHECK(cell.columnSpan()==1);
        }
        CHECK(emptyCells.markdown()==source);
        emptyCells.setTextCursor(table->cellAt(1,3).firstCursorPosition());
        QKeyEvent editEmpty(QEvent::KeyPress,0,Qt::NoModifier,QStringLiteral("独立单元格"));QApplication::sendEvent(&emptyCells,&editEmpty);
        const auto data=mdedit::readMarkdownTable(emptyCells.markdown());
        CHECK(data.error.isEmpty());CHECK(data.rows[1].size()==4);CHECK(data.rows[1][1]=="待验证");CHECK(data.rows[1][2].isEmpty());CHECK(data.rows[1][3]=="独立单元格");
    }
    QTemporaryDir dir;CHECK(dir.isValid());const QString path=dir.filePath("full.csv");QByteArray bytes="name,value\n";
    for(int i=0;i<80;++i)bytes+=QByteArray::number(i)+",value"+QByteArray::number(i)+"\n";
    QFile file(path);CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(bytes)==bytes.size());file.close();
    const QString ref="Before.\n\n![Full CSV](full.csv)\n\nAfter.\n";
    widget.setBaseUrl(QUrl::fromLocalFile(dir.path()+"/"));widget.setMarkdown(ref);
    CHECK(live->editTableAt(ref.indexOf("![")));dialog=live->findChild<mdedit::TableEditorDialog *>();CHECK(dialog);
    grid=dialog->findChild<QTableWidget *>("tableCells");CHECK(waitFor([&]{return grid->rowCount()==81;}));
    grid->setItem(80,1,new QTableWidgetItem("changed, \"last\"\nline"));grid->setCurrentCell(80,1);click(dialog,"insertRowAfter");
    grid->setItem(81,0,new QTableWidgetItem("added"));grid->setItem(81,1,new QTableWidgetItem("end"));
    bool saved=false;QObject::connect(dialog,&mdedit::TableEditorDialog::csvSaved,&widget,[&]{saved=true;});click(dialog,"applyTable");CHECK(waitFor([&]{return saved;}));
    CHECK(file.open(QIODevice::ReadOnly));const QByteArray changed=file.readAll();file.close();const auto complete=mdedit::readCsv(changed);
    CHECK(complete.rows.size()==82);CHECK(complete.rows[80][1]=="changed, \"last\"\nline");CHECK(complete.rows.last()[0]=="added");
    CHECK(widget.markdown()==ref);CHECK(!widget.document()->isModified());
    QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    CHECK(live->editTableAt(ref.indexOf("![")));dialog=live->findChild<mdedit::TableEditorDialog *>();CHECK(dialog);
    grid=dialog->findChild<QTableWidget *>("tableCells");CHECK(waitFor([&]{return grid->rowCount()==82;}));
    CHECK(file.open(QIODevice::Append));file.write("external,new\n");file.close();
    grid->setItem(0,0,new QTableWidgetItem("DO NOT OVERWRITE"));click(dialog,"applyTable");
    CHECK(waitFor([&]{return dialog->isEnabled();}));CHECK(dialog->isVisible());
    CHECK(file.open(QIODevice::ReadOnly));const auto external=file.readAll();file.close();CHECK(external==changed+"external,new\n");dialog->reject();
    CHECK(live->moveTableBlockAt(ref.indexOf("!["),1));CHECK(widget.markdown().indexOf("After.")<widget.markdown().indexOf("!["));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==ref);
    return 0;
}
