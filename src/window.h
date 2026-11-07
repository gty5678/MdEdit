#pragma once
#include "editor/live_editor.h"

class Window : public QMainWindow {
    QPlainTextEdit *editor=new NumberedEditor<QPlainTextEdit>;
    LiveEditor *preview=new LiveEditor;
    QStackedWidget *pages=new QStackedWidget;
    QStackedWidget *screens=new QStackedWidget;
    QToolBar *bar;
    QAction *previewAction;
    QTimer timer,autosave,statsTimer;
    QString path;
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
    bool save();
    void load(const QString &file);
protected:
    void closeEvent(QCloseEvent *e)override;
};

