#pragma once
#include "editor/markdown_editor_widget.h"

class Window : public QMainWindow {
    MarkdownEditorWidget *editingSurface=new MarkdownEditorWidget;
    QStackedWidget *screens=new QStackedWidget;
    QTimer autosave,statsTimer;
    bool loading=false;
    QString path;
    bool documentOpen=false;
    QMenu *recentMenu=nullptr;
    void prepareSwitch(std::function<void()> next);
    void chooseSaveAs(std::function<void()> next={});
    void refreshRecentFiles();
    void rememberFile();
    QLabel *documentStats=new QLabel;
    int baseFontSize=14;
    const QFont defaultUiFont=QApplication::font();
public:
    Window();
    void applyBaseFontSize(int size);
    void showSettings();
    QPair<int,int> selection() const;
    void selectSource(int anchor,int position);
    void replaceSource(int start,int end,const QString &text,int anchor,int position);
    void formatSelection(const QString &left,const QString &right);
    void clipboardParagraph(bool cut);
    void indentSelection(bool outdent);
    void showFind(bool replace);
    void updateDocumentStats();
    void updateTitle();
    void render();
    void newDocument();
    bool saveAs(const QString &target);
    bool save();
    void load(const QString &file);
protected:
    void closeEvent(QCloseEvent *e)override;
};

