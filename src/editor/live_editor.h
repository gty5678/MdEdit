#pragma once
#include "numbered_editor.h"
#include <optional>
#include "resources/resource_manager.h"
#include "tables/table_data.h"
#include "markdown/block_parser.h"

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
    QTimer resourceRefreshTimer;
    int active=-1;
    bool rebuilding=false;
    bool backgroundRefresh=false;
    bool movingCursor=false;
    bool movingVertically=false;
    bool sourceExposed=false;
    qreal resourceEditSlotHeight=0;
    QPointer<QTextTable> nativeTable;
    mdedit::TableData nativeData;
    QList<QStringList> nativeExports;
    int nativeStart=-1,nativeEnd=-1;
    bool bindNativeTable(const QTextCursor &cursor);
    bool syncNativeTable();
    void revealNativeCell(const QTextCursor &cursor);
    int sourceCellRow=-1,sourceCellColumn=-1;
    void layoutNativeTable(QTextTable *table);
    void layoutNativeTables();
    bool nativeSelection() const;
    QTimer axisDragScroll;
    struct AxisControls {int start;QPointer<QTextTable> table;QRectF frame;QList<QRectF> rows,columns;QRectF addRow,addColumn;};
    QList<AxisControls> axisControls() const;
    int axisSource=-1,axisIndex=-1,axisBoundary=-1;
    bool axisRow=true,axisPressed=false,axisStarted=false;
    QPoint axisOrigin,axisPoint,tableHoverPoint{-100,-100};
    QString axisSnapshot;
    qreal axisDropPosition=0;
    void updateAxisDrop();
    void cancelAxisDrag();
    struct BlockControls { int region; QRectF open, edit; QUrl file; };
    QList<BlockControls> blockControls() const;
    struct CodeCopyControl { int region; QRectF rect; };
    QList<CodeCopyControl> codeCopyControls() const;
    int copiedCodeSource=-1;
    QTimer copyFeedbackTimer;
    QString pressedLink;
    QPoint linkPressPosition;
    QString clickableAnchorAt(const QPoint &point) const;
    QString oldText;
    QCache<QString,QTextDocument> renderedMarkdown{128};
    QSet<QString> resourceRenderKeys;
    mdedit::ResourceManager resourceManager;
    int resourceSourceEditStart=-1;
    QHash<QUrl,int> pdfPages;
    QHash<QUrl,qreal> pdfOffsets;
    void refreshPdfPreview();
    void handlePdfAction(const QUrl &action);
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
    bool formatNativeSelection(const QString &left,const QString &right);
    bool clipboardNativeCell(bool cut);
    bool editTableAt(int sourceOffset);
    bool editResourceSourceAt(int sourceOffset);
    bool moveTableBlockAt(int sourceOffset,int direction);
    bool moveMarkdownTableTo(int sourceOffset,int boundary);
    bool reorderMarkdownAxisAt(int sourceOffset,bool row,int from,int boundary);
    bool changeMarkdownTableAt(int sourceOffset,int row,int column,const QString &operation);
    void rebuild(int chosen,int offset,int forcedTarget=-1);
protected:
    bool viewportEvent(QEvent *event)override;
    void paintEvent(QPaintEvent *event)override;
    void wheelEvent(QWheelEvent *event)override;
    void mouseMoveEvent(QMouseEvent *event)override;
    void mousePressEvent(QMouseEvent *event)override;
    void mouseReleaseEvent(QMouseEvent *event)override;
    void mouseDoubleClickEvent(QMouseEvent *event)override;
    void contextMenuEvent(QContextMenuEvent *event)override;
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
    bool isResourceRegion(int index) const;
    void preserveResourceSlot();
    static QList<QPair<int,bool>> inlinePieces(const QString &text);
    static bool isTextBlock(const QTextBlock &block);
    static const QRegularExpression &listMarker();
    static bool isMultilineParagraph(const QString &text);
    void styleActiveSource();
    int sourcePosition(int pos)const;
    void exposeSource(int begin,int end);
    void prepareSelection();
};

