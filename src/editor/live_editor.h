#pragma once
#include "numbered_editor.h"
#include <optional>

// The source remains authoritative; inline editing exposes just the active span.
class LiveEditor : public NumberedEditor<QTextBrowser> {
    struct Region {
        int sourceStart, sourceEnd, start, end, contentEnd=-1;
        bool inlinePart=false, continuesBefore=false, continuesAfter=false;
        std::optional<QTextBlockFormat> listItemFormat;
        bool codeBlock=false;
        int codeFenceStart=-1;
        bool listItem=false;
    };
    QList<Region> regions;
    QString source;
    int active=-1;
    bool rebuilding=false;
    bool movingCursor=false;
    bool movingVertically=false;
    bool sourceExposed=false;
    QString pressedLink;
    QPoint linkPressPosition;
    QString clickableAnchorAt(const QPoint &point) const;
    QString oldText;
    QCache<QString,QTextDocument> renderedMarkdown{128};
    QFont renderedFont;
    QUrl renderedBaseUrl;
public:
    std::function<void(const QString &)> sourceChanged;
    std::function<void(bool)> history;
    explicit LiveEditor();
    void setSource(const QString &text);
    QString markdown()const;
    void refreshAppearance();
    QPair<int,int> sourceSelection()const;
    void restoreSourceSelection(int anchor,int position);
    int sourceLineCount()const override;
    int lineNumberAt(const QTextCursor &cursor)const override;
    void rebuild(int chosen,int offset,int forcedTarget=-1);
protected:
    void paintEvent(QPaintEvent *event)override;
    void mouseMoveEvent(QMouseEvent *event)override;
    void mousePressEvent(QMouseEvent *event)override;
    void mouseReleaseEvent(QMouseEvent *event)override;
    void focusInEvent(QFocusEvent *event)override;
    void focusOutEvent(QFocusEvent *event)override;
    void keyPressEvent(QKeyEvent *event)override;
    void insertFromMimeData(const QMimeData *mime)override;
    void inputMethodEvent(QInputMethodEvent *event)override;
private:
    std::optional<QPair<int,int>> listPrefixAt(int at)const;
    QTextCharFormat codeCharacters()const;
    QTextBlockFormat codeBlockFormat()const;
    static QTextCharFormat headingStyle(int level,const QFont &font,QTextBlockFormat &format);
    static bool isStandaloneImage(const QTextBlock &block);
    static void fitInlineImages(const QTextBlock &block,QTextDocument *document);
    const QTextDocument *renderedDocument(const QString &text,bool joinedParagraph,bool inlinePart=false);
    int regionAt(int pos)const;
    void activateCaretRegion();
    static QList<QPair<int,bool>> inlinePieces(const QString &text);
    static bool isTextBlock(const QTextBlock &block);
    static const QRegularExpression &listMarker();
    static bool isMultilineParagraph(const QString &text);
    void styleActiveSource();
    int sourcePosition(int pos)const;
    void exposeSource(int begin,int end);
    void prepareSelection();
};

