#pragma once
#include <QtWidgets>

inline const QColor highlightBackground("#fff2a8");
inline const QColor quoteBackground("#eee9f7");
inline const QColor codeBlockBackground("#e8e8e8");
inline constexpr int formulaPaintHeight=QTextFormat::UserProperty+1;
inline constexpr int formulaPaintAscent=QTextFormat::UserProperty+2;
inline constexpr int formulaVector=QTextFormat::UserProperty+3;
inline constexpr int formulaVectorSize=QTextFormat::UserProperty+4;
inline constexpr int formulaMarkdown=QTextFormat::UserProperty+5;
struct InlineEditingStyle { int start,length; QTextCharFormat format; };
QList<InlineEditingStyle> inlineEditingStyles(const QString &source);
qreal bodyLineHeight(const QFont &font);
void setBodyLineHeight(QTextBlockFormat &format,const QFont &font);
bool isCodeBlock(const QTextBlockFormat &format);
QTextCharFormat headingCharacters(int level,const QFont &base);
void installFormulaImageHandler(QTextDocument *document);
int renderMarkdown(QTextDocument *document,const QString &source,QStringList &errors,bool live=false);
