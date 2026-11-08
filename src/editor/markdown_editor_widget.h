#pragma once
#include "live_editor.h"

// A complete editor surface with no file I/O, settings persistence or menu bar.
class MarkdownEditorWidget : public QWidget {
    Q_OBJECT
    QPlainTextEdit *editor=new NumberedEditor<QPlainTextEdit>;
    LiveEditor *preview=new LiveEditor;
    QStackedWidget *pages=new QStackedWidget;
    QAction *previewAction;
    QList<QAction *> commands;
    QTimer timer;
    QUrl baseUrl;
    int baseFontSize=14;
    const QFont defaultUiFont=QApplication::font();
public:
    explicit MarkdownEditorWidget(QWidget *parent=nullptr);
    QString markdown() const;
    void setMarkdown(const QString &text);
    QTextDocument *document() const;
    void setBaseUrl(const QUrl &url);
    bool isLiveMode() const;
    void setLiveMode(bool enabled);
    QAction *liveModeAction() const { return previewAction; }
    QList<QAction *> editingActions() const { return commands; }
    void applyBaseFontSize(int size);
    QPair<int,int> selection() const;
    void selectSource(int anchor,int position);
    void replaceSource(int start,int end,const QString &text,int anchor,int position);
    void formatSelection(const QString &left,const QString &right);
    void clipboardParagraph(bool cut);
    void indentSelection(bool outdent);
    void showFind(bool replace);
    void render();
signals:
    void textChanged();
    void modificationChanged(bool modified);
    void liveModeChanged(bool enabled);
};
