#include "window.h"
bool readFile(const QString &path,QByteArray *bytes){QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;*bytes=file.readAll();return file.error()==QFileDevice::NoError;}
#define CHECK(condition) do{if(!(condition)){qCritical()<<"Failed at"<<__LINE__<<#condition;return 1;}}while(false)

int main(int argc,char **argv){
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("MdEditTests");QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    const QString data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    struct Cleanup{QString path;~Cleanup(){QDir(path).removeRecursively();}} cleanup{data};
    QTemporaryDir dir;CHECK(dir.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    const QString text="# 第一章\n\nbody one\n\n## 子章节\n\nchild body\n\n# 第二章\n\nlast body\n";
    const QString first=dir.filePath("first.md"),second=dir.filePath("second.md");
    {
        Window window;window.newDocument();CHECK(window.windowTitle().contains("未命名"));
        auto *editor=window.findChild<MarkdownEditorWidget *>();CHECK(editor);
        window.replaceSource(0,0,text,0,0);CHECK(editor->document()->isModified());
        window.newDocument();auto *prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);
        prompt->button(QMessageBox::Cancel)->click();CHECK(editor->markdown()==text);app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
        CHECK(window.saveAs(first));CHECK(!editor->document()->isModified());QByteArray bytes;CHECK(readFile(first,&bytes)&&bytes==text.toUtf8());
        window.replaceSource(text.size(),text.size(),"extra",0,0);CHECK(window.saveAs(second));
        CHECK(readFile(first,&bytes)&&bytes==text.toUtf8());CHECK(readFile(second,&bytes)&&bytes.endsWith("extra"));
        CHECK(QSettings().value("files/recent").toStringList()==QStringList({second,first}));
        window.newDocument();CHECK(editor->markdown().isEmpty());CHECK(window.windowTitle().contains("未命名"));
        window.replaceSource(0,0,"save before new",0,0);window.newDocument();
        prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);prompt->button(QMessageBox::Save)->click();
        auto *picker=window.findChild<QFileDialog *>("saveAsDialog");CHECK(picker);
        const QString third=dir.filePath("third.md");picker->selectFile(third);QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);
        CHECK(readFile(third,&bytes)&&bytes=="save before new");CHECK(editor->markdown().isEmpty());
        app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
        window.replaceSource(0,0,"discard this",0,0);window.newDocument();
        prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);prompt->button(QMessageBox::Discard)->click();CHECK(editor->markdown().isEmpty());
    }
    {
        Window window;QMenu *recent=nullptr;
        for(auto *menu:window.findChildren<QMenu *>())if(menu->title()=="最近文件")recent=menu;
        CHECK(recent);CHECK(recent->actions().first()->text()==dir.filePath("third.md"));
        for(auto *action:recent->actions())if(action->text()==second){action->trigger();break;}
        CHECK(window.windowTitle().startsWith("second.md"));
        CHECK(QFile::remove(first));
        for(auto *action:recent->actions())if(action->text()==first){action->trigger();break;}
        CHECK(!QSettings().value("files/recent").toStringList().contains(first));
    }
    {
        Window window;window.newDocument();window.replaceSource(0,0,"unsaved",0,0);
        CHECK(!window.saveAs(dir.filePath("missing/fail.md")));
        CHECK(window.findChild<MarkdownEditorWidget *>()->document()->isModified());
    }
    return 0;
}
