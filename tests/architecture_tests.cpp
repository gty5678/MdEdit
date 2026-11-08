#include <QtWidgets>
#include "editor/markdown/block_parser.h"
#include "editor/resources/resource_manager.h"
#include "editor/markdown_editor_widget.h"
#include "editor/live_editor.h"
#include "editor/view/markdown_preview_widget.h"
#include <iostream>
#define CHECK(x) do { if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';return 1;} } while(false)
int main(int argc,char **argv){
    QApplication app(argc,argv);
    const QString source=QString::fromUtf8("# 中文 😀\n\n段落\n\n```md\n![fake](none.png)\n```\n\n![表格](<data file.csv>)\n");
    const auto blocks=mdedit::MarkdownParser::parse(source);
    CHECK(blocks.size()==4);
    CHECK(blocks[2].kind==mdedit::BlockKind::Code);
    CHECK(blocks[3].resource->uri=="data file.csv");
    CHECK(blocks[3].source.begin==source.indexOf("![表格]"));
    // Long documents have their real content and geometry before scrolling.
    LiveEditor lazy;lazy.resize(900,700);
    QString large;
    for(int i=0;i<1500;++i)large+=QString("Paragraph **token%1**.\n\n").arg(i);
    lazy.setSource(large);
    CHECK(lazy.markdown()==large);
    CHECK(lazy.toPlainText().contains("token1499"));
    lazy.show();app.processEvents();
    CHECK(lazy.toPlainText().contains("token0"));
    lazy.verticalScrollBar()->setValue(lazy.verticalScrollBar()->maximum());
    CHECK(lazy.toPlainText().contains("token1499"));
    const int tail=large.indexOf("token1499");
    lazy.restoreSourceSelection(tail,tail+9);
    CHECK(lazy.sourceSelection()==qMakePair(tail,tail+9));
    CHECK(lazy.markdown()==large);
    QTemporaryDir dir;CHECK(dir.isValid());
    QFile csv(dir.filePath("data file.csv"));CHECK(csv.open(QIODevice::WriteOnly));
    csv.write("name,value\n\"hello, world\",\"a\"\"b\"\n");csv.close();
    const QUrl base=QUrl::fromLocalFile(dir.path()+"/");
    const auto url=mdedit::ResourceResolver::resolve("data%20file.csv",base);
    CHECK(url.toLocalFile()==csv.fileName());
    mdedit::ResourceManager manager;
    const QString key=manager.request(url);CHECK(manager.request(url)==key);
    const QString missing=manager.request(QUrl::fromLocalFile(dir.filePath("missing.png")));
    const QString remote=manager.request(QUrl("https://example.com/image.png"));
    QElapsedTimer elapsed;elapsed.start();
    while((!manager.lookup(key)||!manager.lookup(missing)||!manager.lookup(remote))&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(manager.lookup(key));CHECK(manager.lookup(key)->error.isEmpty());
    CHECK(manager.lookup(key)->rows[1]==QStringList({"hello, world","a\"b"}));
    CHECK(manager.lookup(missing)&&!manager.lookup(missing)->error.isEmpty());
    CHECK(manager.lookup(remote)&&!manager.lookup(remote)->error.isEmpty());
    const auto sized=mdedit::MarkdownParser::parse("![Photo|400](photo.png)");
    CHECK(sized.first().resource->width==400);CHECK(sized.first().resource->alt=="Photo");
    CHECK(mdedit::MarkdownParser::parse("![Photo|0](photo.png)").first().resource->width==0);
    QImage photo(800,400,QImage::Format_RGB32);photo.fill(Qt::red);
    CHECK(photo.save(dir.filePath("photo.png")));
    CHECK(photo.save(dir.filePath("attachment.ico"),"PNG"));
    const auto unsupported=manager.request(QUrl::fromLocalFile(dir.filePath("attachment.ico")));
    elapsed.restart();while(!manager.lookup(unsupported)&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(manager.lookup(unsupported)&&!manager.lookup(unsupported)->error.isEmpty());
    CHECK(manager.lookup(unsupported)->image.isNull());
    for(const auto &extension:QStringList{"png","svg","jpg","jpeg","bmp","gif","webp","tif","tiff"})
        CHECK(mdedit::ResourceResolver::isImageResource(QUrl("file:///sample."+extension.toUpper())));
    LiveEditor attachment;attachment.resize(900,700);attachment.document()->setBaseUrl(base);
    attachment.setSource("![Attachment](attachment.ico)");
    CHECK(attachment.toPlainText().contains("attachment.ico"));
    CHECK(!attachment.toPlainText().contains(QChar::ObjectReplacementCharacter));
    CHECK(attachment.document()->size().height()<100);
    bool hasOpenLink=false;
    for(auto block=attachment.document()->begin();block.isValid();block=block.next())
        for(auto it=block.begin();!it.atEnd();++it){
            const QUrl action(it.fragment().charFormat().anchorHref());
            if(action.path()=="/open")hasOpenLink=QUrl(QUrlQuery(action).queryItemValue("file",QUrl::FullyDecoded)).toLocalFile()==dir.filePath("attachment.ico");
        }
    CHECK(hasOpenLink);
    LiveEditor images;images.resize(900,700);images.document()->setBaseUrl(base);
    const QString imageSource="Before.\n\n![Photo|400](photo.png)\n\nAfter.\n";
    images.setSource(imageSource);images.show();
    auto sizedImage=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().startsWith("image-ready:"))return f.width()==400&&f.height()==200;
        }return false;
    };
    elapsed.restart();while(!sizedImage()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(sizedImage());CHECK(images.markdown()==imageSource);
    images.restoreSourceSelection(imageSource.indexOf("!["),imageSource.indexOf("!["));
    CHECK(sizedImage());
    for(auto b=images.document()->begin();b.isValid();b=b.next())if(b.text().contains(QChar::ObjectReplacementCharacter)){
        const auto bounds=images.document()->documentLayout()->blockBoundingRect(b);
        const QPointF pt(bounds.left()+30,bounds.top()-images.verticalScrollBar()->value()+60);
        QMouseEvent click(QEvent::MouseButtonPress,pt,images.viewport()->mapToGlobal(pt.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(images.viewport(),&click);break;
    }
    CHECK(sizedImage());CHECK(!images.toPlainText().contains("![Photo|400]"));
    CHECK(images.editResourceSourceAt(imageSource.indexOf("![")));
    CHECK(images.toPlainText().contains("![Photo|400]"));
    images.restoreSourceSelection(0,0);CHECK(images.markdown()==imageSource);
    images.setSource("Before.\n\nText ![Inline|200](photo.png) here.\n");
    auto inlineSized=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().endsWith("photo.png"))return f.width()==200&&f.height()==100;
        }return false;
    };
    elapsed.restart();while(!inlineSized()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(inlineSized());
    auto writeText=[&](const QString &name,const QByteArray &bytes){
        QFile file(dir.filePath(name));if(!file.open(QIODevice::WriteOnly))return QString();
        file.write(bytes);file.close();return manager.request(QUrl::fromLocalFile(file.fileName()));
    };
    const auto txt=writeText("note.TXT",QString::fromUtf8("中文 <b>plain</b>\r\nsecond").toUtf8());
    const auto json=writeText("data.json","{\"name\": \"value\"}");
    const auto empty=writeText("empty.log",{});
    const auto utf16=writeText("unicode.txt",QByteArray::fromHex("fffe2d4e8765"));
    const auto badText=writeText("binary.txt",QByteArray("a\0b",3));
    const auto longText=writeText("long.log",QByteArray("line\n").repeated(100));
    auto textReady=[&]{return manager.lookup(txt)&&manager.lookup(json)&&manager.lookup(empty)&&manager.lookup(utf16)&&manager.lookup(badText)&&manager.lookup(longText);};
    elapsed.restart();while(!textReady()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(textReady());
    CHECK(manager.lookup(txt)->text==QString::fromUtf8("中文 <b>plain</b>\nsecond"));
    CHECK(manager.lookup(json)->text.has_value());
    CHECK(manager.lookup(empty)->text.has_value()&&manager.lookup(empty)->text->isEmpty());
    CHECK(manager.lookup(utf16)->text==QString::fromUtf8("中文"));
    CHECK(!manager.lookup(badText)->error.isEmpty());
    CHECK(manager.lookup(longText)->textTruncated);
    const QString textSource="Before.\n\n![说明](note.TXT)\n\nAfter.\n";
    images.setSource(textSource);
    auto textPreviewReady=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().startsWith("text-ready:"))return true;
        }return false;
    };
    elapsed.restart();while(!textPreviewReady()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(textPreviewReady());CHECK(!images.toPlainText().contains("打开原文件"));CHECK(images.markdown()==textSource);
    // The top-right action exposes the attachment source without modifying it.
    for(auto b=images.document()->begin();b.isValid();b=b.next())if(b.text().contains(QChar::ObjectReplacementCharacter)){
        const auto bounds=images.document()->documentLayout()->blockBoundingRect(b);
        const qreal width=qMax(QFontMetricsF(images.font()).horizontalAdvance(QStringLiteral("打开原文件")),QFontMetricsF(images.font()).horizontalAdvance(QStringLiteral("编辑源码")))+16;
        const QPointF pt(qMin(qreal(images.viewport()->width()-8),bounds.right())-width/2,bounds.top()-images.verticalScrollBar()->value()+8);
        QMouseEvent press(QEvent::MouseButtonPress,pt,images.viewport()->mapToGlobal(pt.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(images.viewport(),&press);break;
    }
    CHECK(images.toPlainText().contains("![说明](note.TXT)"));
    CHECK(images.markdown()==textSource);
    images.restoreSourceSelection(textSource.indexOf("!["),textSource.indexOf("!["));
    CHECK(images.toPlainText().contains("![说明](note.TXT)"));
    images.restoreSourceSelection(0,0);CHECK(images.markdown()==textSource);
    // Keep the comprehensive document's real resource fixtures loadable.
    const QDir project(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath(".."));
    QFile comprehensive(project.filePath(QString::fromUtf8("综合测试.md")));
    CHECK(comprehensive.open(QIODevice::ReadOnly));
    const auto resourceBlocks=mdedit::MarkdownParser::parse(QString::fromUtf8(comprehensive.readAll()));
    const QUrl fixtureBase=QUrl::fromLocalFile(project.absolutePath()+"/");
    QList<QPair<QString,bool>> fixtureKeys;
    for(const auto &block:resourceBlocks)if(block.resource){
        const auto fixtureUrl=mdedit::ResourceResolver::resolve(block.resource->uri,fixtureBase);
        const bool expectedError=!fixtureUrl.isLocalFile()||fixtureUrl.path().contains("/errors/")||fixtureUrl.path().endsWith("does-not-exist.png");
        fixtureKeys.append({manager.request(fixtureUrl),expectedError});
    }
    CHECK(fixtureKeys.size()>40);
    auto fixturesReady=[&]{for(const auto &entry:fixtureKeys)if(!manager.lookup(entry.first))return false;return true;};
    elapsed.restart();while(!fixturesReady()&&elapsed.elapsed()<10000){app.processEvents();QThread::msleep(1);}
    CHECK(fixturesReady());
    for(const auto &entry:fixtureKeys){
        const auto *snapshot=manager.lookup(entry.first);
        if(snapshot->error.isEmpty()==entry.second)qWarning()<<entry.first<<snapshot->error;
        CHECK(snapshot->error.isEmpty()!=entry.second);
    }
    MarkdownEditorWidget widget;widget.setMarkdown(source);widget.setLiveMode(false);
    CHECK(widget.findChildren<QSplitter *>().isEmpty());
    widget.selectSource(blocks[3].source.begin,blocks[3].source.begin);CHECK(widget.selection().first==blocks[3].source.begin);
    widget.resize(900,600);widget.show();app.processEvents();
    mdedit::MarkdownPreviewWidget standalone;standalone.setSource(source,base);standalone.resize(450,600);standalone.show();
    auto *preview=&standalone;
    QObject::connect(preview,&mdedit::MarkdownPreviewWidget::sourceActivated,&widget,[&](int position){widget.selectSource(position,position);});
    QMouseEvent click(QEvent::MouseButtonPress,QPointF(30,30),QPointF(30,30),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(preview->viewport(),&click);CHECK(widget.selection()==qMakePair(0,0));
    CHECK(!widget.grab().isNull());
    widget.replaceSource(0,0,"new",3,3);CHECK(widget.markdown().startsWith("new#"));
    widget.document()->undo();CHECK(widget.markdown()==source);
    widget.setLiveMode(false);CHECK(!widget.isLiveMode());CHECK(widget.markdown()==source);
    widget.setLiveMode(true);widget.setLiveMode(false);CHECK(widget.markdown()==source);
    return 0;
}
