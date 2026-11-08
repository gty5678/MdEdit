#pragma once
#include <QtWidgets>
#include "table_data.h"
namespace mdedit {
class TableEditorDialog : public QDialog {
    Q_OBJECT
    QTableWidget *grid=new QTableWidget;
    QLabel *message=new QLabel;
    QPushButton *saveButton=new QPushButton("应用");
    TableData data;
    QString filePath;
    QByteArray original;
    QThreadPool pool;
    bool saving=false;
    void populate();
    void save();
    void insertAxis(bool row,bool after);
    void removeAxis(bool row);
    void moveAxis(bool row,int delta);
public:
    explicit TableEditorDialog(QWidget *parent=nullptr);
    ~TableEditorDialog() override;
    std::function<bool(const QString &)> applyMarkdown;
    void setMarkdown(const QString &source);
    void loadCsv(const QString &path);
    void reject() override;
signals:
    void markdownApplied(const QString &text);
    void csvSaved();
};
}
