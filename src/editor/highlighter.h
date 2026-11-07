#pragma once
#include "../markdown/scanner.h"

class Highlighter : public QSyntaxHighlighter {
    bool spansDirty=true;
    QList<Span> spans;
    struct Token { int start,length; QColor color; };
    QList<Token> tokens;
    QHash<int,int> codeStates;
public:
    explicit Highlighter(QTextDocument *d);
    void highlightBlock(const QString &text) override;
};

