#include "table_data.h"
namespace mdedit {
TableData readCsv(const QByteArray &bytes){
    TableData data;data.bom=bytes.startsWith("\xef\xbb\xbf");
    QStringDecoder decoder(QStringDecoder::Utf8);const QString text=decoder.decode(bytes);
    if(decoder.hasError()){data.error="仅支持 UTF-8 CSV，请先转换文件编码。";return data;}
    if(bytes.contains("\r\n"))data.newline="\r\n";
    QString field;QStringList row;bool quoted=false,closed=false;
    auto endField=[&]{row.append(field);field.clear();closed=false;};
    auto endRow=[&]{endField();data.rows.append(row);row.clear();};
    for(int i=0;i<text.size();++i){
        QChar c=text[i];if(i==0&&c==QChar::ByteOrderMark)continue;
        if(quoted){if(c=='"'){if(i+1<text.size()&&text[i+1]=='"'){field+='"';++i;}else{quoted=false;closed=true;}}else field+=c;}
        else if(c==',')endField();
        else if(c=='\n'||c=='\r'){if(c=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;endRow();}
        else if(c=='"'&&field.isEmpty()&&!closed)quoted=true;
        else if(closed||c=='"'){data.error="CSV 引号格式不正确。";return data;}
        else field+=c;
        if(data.rows.size()>10000||row.size()>256){data.error="表格编辑上限为 10000 行、256 列。";return data;}
    }
    if(quoted){data.error="CSV 引号未闭合。";return data;}
    if(!row.isEmpty()||!field.isEmpty()||closed)endRow();
    if(data.rows.size()>10000){data.error="表格编辑上限为 10000 行。";return data;}
    for(const auto &r:data.rows)if(r.size()>256){data.error="表格编辑上限为 256 列。";return data;}
    if(data.rows.isEmpty())data.rows.append(QStringList{QString()});
    return data;
}
QByteArray writeCsv(const TableData &data){
    QStringList lines;
    for(const auto &row:data.rows){QStringList cells;for(QString cell:row){if(cell.contains(QRegularExpression("[,\"\r\n]"))){cell.replace("\"","\"\"");cell='"'+cell+'"';}cells.append(cell);}lines.append(cells.join(','));}
    return (data.bom?QByteArray("\xef\xbb\xbf"):QByteArray())+(lines.join(data.newline)+data.newline).toUtf8();
}
static QStringList markdownCells(QString line){
    line=line.trimmed();if(line.startsWith('|'))line.remove(0,1);
    if(line.endsWith('|')&&(line.size()<2||line[line.size()-2]!='\\'))line.chop(1);
    QStringList cells;QString cell;bool escaped=false;
    for(const QChar c:line){if(c=='|'&&!escaped){cells.append(cell.trimmed());cell.clear();}else cell+=c;
        if(c=='\\')escaped=!escaped;else escaped=false;}
    cells.append(cell.trimmed());return cells;
}
TableData readMarkdownTable(const QString &source){
    TableData data;const auto lines=source.trimmed().split('\n');
    if(lines.size()<2){data.error="未找到 Markdown 表格。";return data;}
    data.separators=markdownCells(lines[1]);
    static const QRegularExpression separator("^:?-{3,}:?$");
    for(const auto &cell:data.separators)if(!separator.match(cell).hasMatch()){data.error="表格分隔行格式不正确。";return data;}
    for(int i=0;i<lines.size();++i)if(i!=1)data.rows.append(markdownCells(lines[i]));
    return data;
}
QString writeMarkdownTable(const TableData &data){
    QStringList lines;
    for(int r=0;r<data.rows.size();++r){
        QStringList cells;
        for(QString cell:data.rows[r]){cell.replace('\n',"<br>");cell.replace('\r',"");
            QString escaped;int slashes=0;
            for(QChar c:cell){if(c=='|'&&slashes%2==0)escaped+='\\';escaped+=c;if(c=='\\')++slashes;else slashes=0;}
            cells.append(escaped);
        }
        lines.append("| "+cells.join(" | ")+" |");
        if(r==0){QStringList separators;for(int c=0;c<cells.size();++c)separators.append(data.separators.value(c,"---"));lines.append("| "+separators.join(" | ")+" |");}
    }
    return lines.join('\n');
}
}
