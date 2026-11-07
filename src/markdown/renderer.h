#pragma once
#include <QtWidgets>

inline const QColor quoteBackground("#eee9f7");
inline constexpr int formulaPaintHeight=QTextFormat::UserProperty+1;
inline constexpr int formulaPaintAscent=QTextFormat::UserProperty+2;
qreal bodyLineHeight(const QFont &font);
void setBodyLineHeight(QTextBlockFormat &format,const QFont &font);
bool isCodeBlock(const QTextBlockFormat &format);
QTextCharFormat headingCharacters(int level,const QFont &base);
void installFormulaImageHandler(QTextDocument *document);
int renderMarkdown(QTextDocument *document,const QString &source,QStringList &errors,bool live=false);
