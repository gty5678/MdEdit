#include "app_tests.h"
#include "editor/highlighter.h"
#include "window.h"
#include "markdown/renderer.h"
#include "markdown/scanner.h"

std::optional<int> runAppTests(QApplication &app){
    if(app.arguments().contains("--code-background-test")){
        LiveEditor editor;editor.resize(640,480);
        editor.document()->setDocumentMargin(24);
        const QString source="Before\n\n```cpp\nint x = 1;\n\n"+QString("value ").repeated(30)+"\n```\n\nAfter";
        editor.setSource(source);editor.show();app.processEvents();
        auto verify=[&]{
            const auto first=editor.document()->find("```").block();
            const auto last=editor.document()->find("```",editor.document()->characterCount()-1,QTextDocument::FindBackward).block();
            const auto image=editor.viewport()->grab().toImage();
            image.save("code-background.png");
            const qreal scale=image.devicePixelRatio();
            const int x=qRound((editor.viewport()->width()-32)*scale);
            const int top=qCeil(editor.document()->documentLayout()->blockBoundingRect(first).top());
            const int bottom=qFloor(editor.document()->documentLayout()->blockBoundingRect(last).bottom())-1;
            for(int y=top;y<=bottom;++y)
                if(image.pixelColor(x,qRound(y*scale))!=QColor("#f3f5f4"))return false;
            return editor.markdown()==source;
        };
        if(!verify())return 7;
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(3)));
        app.processEvents();
        if(!verify())return 8;
        editor.grab().save("code-background.png");
        return 0;
    }
    if(!app.arguments().contains("--table-gutter-test") &&
       !app.arguments().contains("--edit-menu-test") &&
       !app.arguments().contains("--smoke-test"))return std::nullopt;
    if(app.arguments().contains("--table-gutter-test")){
        LiveEditor tableView;tableView.setFont(QFont(previewFont,11));tableView.resize(640,480);tableView.show();
        const QString markdown="Before\n\n| Name | Value | Extra |\n| --- | --- | --- |\n| Alpha | One | A |\n| Beta | Two | B |\n\nAfter";
        tableView.setSource(markdown);app.processEvents();
        int labels=0,cells=0;
        for(auto block=tableView.document()->begin();block.isValid();block=block.next()){
            QTextCursor cursor(block);
            if(const auto table=cursor.currentTable()){
                const auto cell=table->cellAt(cursor);
                const int label=tableView.gutterLineNumberAt(cursor);
                if((label>0)!=(cell.column()==0))return 5;
                if(label>0&&label!=(cell.row()==0?3:cell.row()+4))return 7;
                labels+=label>0;++cells;
            }
        }
        if(cells!=9||labels!=3||tableView.markdown()!=markdown)return 6;
        tableView.grab().save("table-gutter.png");
        return 0;
    }
    int result=0;
    if(app.arguments().contains("--edit-menu-test")){
        Window commands;
        auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
        auto *live=commands.findChild<QTextBrowser *>("markdownPreview");
        bool ok=source&&live;
        const QString original="alpha\nbeta\n\n# Tail heading";
        QTemporaryFile menuFixture;
        if(!menuFixture.open())return 4;
        menuFixture.write(original.toUtf8());menuFixture.flush();
        commands.load(menuFixture.fileName());
        for(const auto reason:{Qt::PopupFocusReason,Qt::MenuBarFocusReason,Qt::OtherFocusReason}){
            source->setPlainText(original);commands.selectSource(1,4);
            QFocusEvent focusOut(QEvent::FocusOut,reason);
            QApplication::sendEvent(live,&focusOut);
            ok=ok&&commands.selection()==qMakePair(1,4);
            for(auto *action:commands.findChildren<QAction *>())if(action->text()==QString::fromUtf8("加粗"))action->trigger();
            ok=ok&&source->toPlainText()=="a**lph**a\nbeta\n\n# Tail heading"&&
                commands.selection()==qMakePair(3,6)&&!live->toPlainText().contains("# Tail heading");
        }
        for(const auto &markers:QList<QPair<QString,QString>>{{"**","**"},{"*","*"},{"`","`"},{"[","](url)"}}){
            source->setPlainText(original);commands.selectSource(0,5);
            commands.formatSelection(markers.first,markers.second);
            ok=ok&&source->toPlainText().startsWith(markers.first+"alpha"+markers.second)&&
                commands.selection()==qMakePair(int(markers.first.size()),int(markers.first.size())+5)&&
                !live->toPlainText().contains("# Tail heading");
        }
        source->setPlainText(original);commands.selectSource(0,6);commands.indentSelection(false);
        ok=ok&&source->toPlainText().startsWith("    alpha")&&!live->toPlainText().contains("# Tail heading");
        commands.indentSelection(true);
        ok=ok&&source->toPlainText()==original&&!live->toPlainText().contains("# Tail heading");
        commands.selectSource(6,10);commands.clipboardParagraph(false);
        ok=ok&&QApplication::clipboard()->text()=="beta"&&!live->toPlainText().contains("# Tail heading");
        commands.clipboardParagraph(true);
        ok=ok&&!source->toPlainText().contains("beta")&&!live->toPlainText().contains("# Tail heading");
        commands.selectSource(source->toPlainText().size(),0);
        ok=ok&&!live->toPlainText().contains("# Tail heading");
        source->setPlainText(original);commands.selectSource(0,0);commands.showFind(true);
        auto *dialog=commands.findChild<QDialog *>("findDialog");
        if(!dialog)dialog=commands.findChild<QDialog *>();
        const auto fields=dialog->findChildren<QLineEdit *>();
        fields[0]->setText("beta");fields[1]->setText("changed");
        auto click=[&](const QString &label){for(auto *button:dialog->findChildren<QPushButton *>())if(button->text()==label)button->click();};
        click("下一个");
        ok=ok&&commands.selection()==qMakePair(6,10)&&!live->toPlainText().contains("# Tail heading");
        click("全部替换");
        ok=ok&&source->toPlainText().contains("changed")&&!live->toPlainText().contains("# Tail heading");
        dialog->close();
        QString deletionDetails;
        {
            LiveEditor deletionProbe;
            const QString inlineSource="Before **hh** and **keep** after\n\n# Tail heading";
            for(const int key:{Qt::Key_Backspace,Qt::Key_Delete}){
                deletionProbe.setSource(inlineSource);
                deletionProbe.restoreSourceSelection(7,13);
                QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
                QApplication::sendEvent(&deletionProbe,&remove);
                deletionDetails+=QString::number(key)+" SOURCE: "+deletionProbe.markdown()+" VIEW: "+deletionProbe.toPlainText()+"\n";
                ok=ok&&deletionProbe.markdown()=="Before  and **keep** after\n\n# Tail heading"&&
                    !deletionProbe.toPlainText().contains("**keep**")&&
                    !deletionProbe.toPlainText().contains("# Tail heading");
            }
            deletionProbe.setSource(inlineSource);
            deletionProbe.restoreSourceSelection(7,13);
            QInputMethodEvent commit;commit.setCommitString("text");
            QApplication::sendEvent(&deletionProbe,&commit);
            deletionDetails+="IME SOURCE: "+deletionProbe.markdown()+" VIEW: "+deletionProbe.toPlainText()+"\n";
            ok=ok&&deletionProbe.markdown()=="Before text and **keep** after\n\n# Tail heading"&&
                !deletionProbe.toPlainText().contains("# Tail heading");
            deletionProbe.setSource("Before **keep** and **unfinished\n\n# Tail heading");
            deletionProbe.restoreSourceSelection(30,30);
            deletionDetails+="UNFINISHED VIEW: "+deletionProbe.toPlainText()+"\n";
            ok=ok&&!deletionProbe.toPlainText().contains("**keep**")&&
                !deletionProbe.toPlainText().contains("# Tail heading");
        }
        QFile report(QDir::current().absoluteFilePath("edit-menu-result.txt"));
        if(report.open(QIODevice::WriteOnly)){report.write(ok?"PASS\n":"FAIL\n");report.write(deletionDetails.toUtf8());}
        return ok?0:1;
    }
    if(app.arguments().contains("--smoke-test")){
        QTextBrowser view;view.resize(900,700);view.setFont(QFont(previewFont,13));QStringList errors;
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
        {
            Window commands;
            auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
            auto *mode=commands.findChild<QAction *>("livePreviewAction");
            for(bool liveMode:{true,false}){
                mode->setChecked(liveMode);source->setPlainText("alpha\nbeta");
                commands.selectSource(0,5);commands.formatSelection("**","**");
                images=checkLive(source->toPlainText()=="**alpha**\nbeta"&&commands.selection()==qMakePair(2,7),__LINE__)&&images;
                commands.formatSelection("**","**");
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
                commands.selectSource(0,6);commands.indentSelection(false);
                images=checkLive(source->toPlainText()=="    alpha\nbeta"&&commands.selection()==qMakePair(4,10),__LINE__)&&images;
                commands.indentSelection(true);
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
                commands.selectSource(7,7);commands.clipboardParagraph(true);
                images=checkLive(source->toPlainText()=="alpha\n"&&QApplication::clipboard()->text()=="beta",__LINE__)&&images;
                source->undo();commands.render();
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
            }
        }
        {
            Window commands;
            auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
            source->setPlainText("Alpha alpha");commands.selectSource(0,0);commands.showFind(true);
            auto *dialog=commands.findChild<QDialog *>();
            const auto fields=dialog->findChildren<QLineEdit *>();fields[0]->setText("alpha");fields[1]->setText("X");
            auto click=[dialog](const QString &label){for(auto *button:dialog->findChildren<QPushButton *>())if(button->text()==label)button->click();};
            click("下一个");images=checkLive(commands.selection()==qMakePair(0,5),__LINE__)&&images;
            click("下一个");images=checkLive(commands.selection()==qMakePair(6,11),__LINE__)&&images;
            click("下一个");images=checkLive(commands.selection()==qMakePair(0,5),__LINE__)&&images;
            click("全部替换");images=checkLive(source->toPlainText()=="X X",__LINE__)&&images;
            source->undo();images=checkLive(source->toPlainText()=="Alpha alpha",__LINE__)&&images;
            dialog->close();
        }
        {
            Window settingsWindow;
            auto *source=settingsWindow.findChild<QPlainTextEdit *>("markdownEditor");
            auto *liveView=static_cast<LiveEditor *>(settingsWindow.findChild<QTextBrowser *>("markdownPreview"));
            source->setPlainText("# Heading\n\nBody");settingsWindow.selectSource(2,9);
            source->document()->setModified(false);
            settingsWindow.showSettings();
            auto *dialog=settingsWindow.findChild<QDialog *>("settingsDialog");
            auto *size=dialog->findChild<QSpinBox *>("baseFontSize");size->setValue(17);
            images=checkLive(source->font().pointSize()==18&&liveView->font().pointSize()==17&&
                source->toPlainText()=="# Heading\n\nBody"&&!source->document()->isModified()&&
                settingsWindow.selection()==qMakePair(2,9),__LINE__)&&images;
            const auto heading=liveView->document()->begin().begin().fragment().charFormat();
            images=checkLive(heading.fontPointSize()==34&&!heading.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            Window restored;
            images=checkLive(restored.findChild<QTextBrowser *>("markdownPreview")->font().pointSize()==17,__LINE__)&&images;
            dialog->close();QSettings().remove("appearance/baseFontSize");
        }
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
                previewCharacters.fontPointSize()>output->font().pointSizeF()&&
                !previewCharacters.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            for(int pos:{0,level+1}){
                QTextCursor sample(output->document());sample.setPosition(pos);sample.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                auto actual=sample.charFormat();
                images=checkLive(actual.fontWeight()==expected.fontWeight()&&
                    actual.fontPointSize()==expected.fontPointSize()&&
                    !actual.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
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
            fileDialog->setDirectory(QFileInfo(filename).absolutePath());
            settle(); // QFileSystemModel populates the directory asynchronously.
            fileDialog->findChild<QLineEdit *>("fileNameEdit")->setText(filename);
            settle();
            QMetaObject::invokeMethod(fileDialog,"accept",Qt::DirectConnection);
            settle();
            images=checkLive(!openButton->isVisible()&&toolbar->isVisible()&&toolbar->actions().contains(toggle)&&
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
            // Keep this baseline-stability fixture on one source line at all
            // supported font sizes; wrapping is covered separately.
            layoutProbe.resize(qMax(640,qCeil(QFontMetricsF(layoutProbe.font()).horizontalAdvance(imageMarkup))+100),480);
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
            probe.setSource("First\n\n\nLast");
            const auto paragraphHeight=[&](int number){
                const auto block=probe.document()->findBlockByNumber(number);
                return probe.document()->documentLayout()->blockBoundingRect(block).height();
            };
            const qreal bodyHeight=paragraphHeight(0);
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0&&
                qAbs(paragraphHeight(2)-bodyHeight)<1.0,__LINE__)&&images;
            QTextCursor emptyLineCaret(probe.document()->findBlockByNumber(1));
            probe.setTextCursor(emptyLineCaret);
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
            probe.insertPlainText(QString::fromUtf8("正文"));
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
            emptyLineCaret=probe.textCursor();emptyLineCaret.movePosition(QTextCursor::StartOfBlock);
            emptyLineCaret.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
            probe.setTextCursor(emptyLineCaret);probe.insertPlainText("");
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
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
            // Every character, including hidden prefixes and item boundaries,
            // remains reachable in both directions without a mouse click.
            for(const QString &list:{QString("- one\n- two\n- three"),
                    QString("1. one\n2. two\n3. three"),
                    QString("- one\n  - two\n- three"),
                    QString("- [x] one\n- [ ] two\n- [x] three")}){
                probe.setSource(list);probe.restoreSourceSelection(0,0);
                for(int direction:{1,-1}){
                    QKeyEvent arrow(QEvent::KeyPress,direction==1?Qt::Key_Right:Qt::Key_Left,Qt::NoModifier);
                    const int start=direction==1?1:int(list.size())-1;
                    const int end=direction==1?int(list.size())+1:-1;
                    for(int at=start;at!=end;at+=direction){
                        QApplication::sendEvent(&probe,&arrow);
                        images=checkLive(probe.sourceSelection()==qMakePair(at,at)&&probe.markdown()==list,__LINE__)&&images;
                    }
                }
                probe.restoreSourceSelection(0,0);
                QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
                QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
                const auto lines=list.split('\n');
                const int second=lines[0].size()+1,third=second+lines[1].size()+1;
                for(int expected:{second,third}){
                    QApplication::sendEvent(&probe,&down);
                    images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
                }
                for(int expected:{second,0}){
                    QApplication::sendEvent(&probe,&up);
                    images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
                }
            }
            // Moving between differently styled blocks must preserve column zero.
            for(const QString &text:{QString("plain\n\n# Heading\n\n> quote\n\n- item\n\nlast"),
                    QString("# Heading\n\nplain\n\n## Smaller\n\nlast")}){
                QList<int> starts{0};
                for(int i=0;i<text.size();++i)if(text[i]=='\n')starts.append(i+1);
                probe.setSource(text);
                for(int direction:{1,-1}){
                    const int first=direction==1?0:starts.size()-1;
                    probe.restoreSourceSelection(starts[first],starts[first]);
                    QKeyEvent arrow(QEvent::KeyPress,direction==1?Qt::Key_Down:Qt::Key_Up,Qt::NoModifier);
                    for(int i=first+direction;i>=0&&i<starts.size();i+=direction){
                        QApplication::sendEvent(&probe,&arrow);
                        images=checkLive(probe.sourceSelection()==qMakePair(starts[i],starts[i])&&
                            probe.markdown()==text,__LINE__)&&images;
                    }
                }
            }
            for(const QString &list:{QString("- **bold** tail"),QString("1. [link](https://example.com)"),
                    QString("- parent\n  - [x] child $x$"),QString("- first\n  continuation\n- last")}){
                probe.setSource(list);probe.restoreSourceSelection(list.size(),list.size());
                QKeyEvent left(QEvent::KeyPress,Qt::Key_Left,Qt::NoModifier);
                int previous=list.size();
                for(int steps=0;steps<list.size()+2&&previous>0;++steps){
                    QApplication::sendEvent(&probe,&left);const int now=probe.sourceSelection().second;
                    images=checkLive(now<previous&&probe.markdown()==list,__LINE__)&&images;
                    if(now>=previous)break;previous=now;
                }
                images=checkLive(probe.sourceSelection()==qMakePair(0,0),__LINE__)&&images;
            }
            probe.setSource("- item");probe.restoreSourceSelection(2,2);
            QKeyEvent selectLeft(QEvent::KeyPress,Qt::Key_Left,Qt::ShiftModifier);
            QApplication::sendEvent(&probe,&selectLeft);QApplication::sendEvent(&probe,&selectLeft);
            images=checkLive(probe.sourceSelection()==qMakePair(2,0),__LINE__)&&images;
            QKeyEvent replaceMarker(QEvent::KeyPress,Qt::Key_Plus,Qt::NoModifier,"+ ");
            QApplication::sendEvent(&probe,&replaceMarker);
            images=checkLive(probe.markdown()=="+ item",__LINE__)&&images;
            const QString uneven="- aaaaaa\n- a\n- aaaaaa";
            probe.setSource(uneven);probe.restoreSourceSelection(5,5);
            QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
            QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
            for(int expected:{12,18}){
                QApplication::sendEvent(&probe,&down);
                images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
            }
            for(int expected:{12,5}){
                QApplication::sendEvent(&probe,&up);
                images=checkLive(probe.sourceSelection()==qMakePair(expected,expected)&&probe.markdown()==uneven,__LINE__)&&images;
            }
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
            LiveEditor codePreview;codePreview.setFont(QFont(previewFont,13));codePreview.resize(760,560);
            codePreview.document()->setDocumentMargin(24);
            codePreview.setSource("# Code preview\n\n```cpp\n#include <iostream>\nint main() {\n    // $x$ stays literal\n    std::cout << \"Hello, Markdown!\";\n    return 0;\n}\n```\n\n```python\ndef square(x):\n    return x ** 2\n\nprint(square(5))\n```\n\nEnd");
            codePreview.grab().save(QDir::current().absoluteFilePath("code-smoke.png"));
        }
        QImage shot(view.size(),QImage::Format_ARGB32);shot.fill(QColor("#ffffff"));view.setStyleSheet("color:#26232d;background:#ffffff;");view.render(&shot);shot.save(QDir::current().absoluteFilePath("math-smoke.png"));
        if(!images){qWarning()<<n<<errors;result=1;}
    }
    return result;
}
