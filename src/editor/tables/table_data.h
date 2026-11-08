#pragma once
#include <QtCore>
namespace mdedit {
struct TableData {
    QList<QStringList> rows;
    QStringList separators;
    QString newline="\n";
    bool bom=false;
    QString error;
};
TableData readCsv(const QByteArray &bytes);
QByteArray writeCsv(const TableData &data);
TableData readMarkdownTable(const QString &source);
QString writeMarkdownTable(const TableData &data);
}
