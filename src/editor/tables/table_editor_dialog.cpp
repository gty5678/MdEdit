#include "table_editor_dialog.h"
namespace mdedit {
TableEditorDialog::TableEditorDialog(QWidget *parent):QDialog(parent){
    setWindowModality(Qt::WindowModal);
    setObjectName("tableEditorDialog");setWindowTitle("编辑表格");resize(940,560);setAttribute(Qt::WA_DeleteOnClose);
    auto *layout=new QVBoxLayout(this);auto *tools=new QHBoxLayout;layout->addLayout(tools);
    auto action=[&](const QString &label,const QString &name,std::function<void()> fn){auto *button=new QPushButton(label);button->setAutoDefault(false);button->setObjectName(name);tools->addWidget(button);connect(button,&QPushButton::clicked,this,fn);};
    action("上方插行","insertRowBefore",[this]{insertAxis(true,false);});action("下方插行","insertRowAfter",[this]{insertAxis(true,true);});
    action("左侧插列","insertColumnBefore",[this]{insertAxis(false,false);});action("右侧插列","insertColumnAfter",[this]{insertAxis(false,true);});
    action("删除行","removeRow",[this]{removeAxis(true);});action("删除列","removeColumn",[this]{removeAxis(false);});
    auto *moves=new QHBoxLayout;layout->addLayout(moves);tools=moves;
    action("行上移","moveRowUp",[this]{moveAxis(true,-1);});action("行下移","moveRowDown",[this]{moveAxis(true,1);});
    action("列左移","moveColumnLeft",[this]{moveAxis(false,-1);});action("列右移","moveColumnRight",[this]{moveAxis(false,1);});tools->addStretch();
    grid->setObjectName("tableCells");grid->setAlternatingRowColors(true);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    grid->horizontalHeader()->setDefaultSectionSize(140);grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed|QAbstractItemView::AnyKeyPressed);
    layout->addWidget(grid);layout->addWidget(message);message->setWordWrap(true);
    auto *buttons=new QHBoxLayout;buttons->addStretch();layout->addLayout(buttons);
    saveButton->setAutoDefault(false);
    auto *cancel=new QPushButton("取消");cancel->setAutoDefault(false);buttons->addWidget(cancel);buttons->addWidget(saveButton);saveButton->setObjectName("applyTable");
    connect(cancel,&QPushButton::clicked,this,&TableEditorDialog::reject);connect(saveButton,&QPushButton::clicked,this,&TableEditorDialog::save);
    pool.setMaxThreadCount(1);
}
TableEditorDialog::~TableEditorDialog(){pool.clear();pool.waitForDone();}
void TableEditorDialog::reject(){if(!saving)QDialog::reject();}
void TableEditorDialog::populate(){
    if(!data.error.isEmpty()){message->setText(data.error);saveButton->setEnabled(false);return;}
    int columns=1;for(const auto &row:data.rows)columns=qMax(columns,int(row.size()));
    if(qint64(columns)*data.rows.size()>200000){message->setText("表格编辑暂支持最多 200000 个单元格。");saveButton->setEnabled(false);return;}
    grid->setRowCount(data.rows.size());grid->setColumnCount(columns);
    for(int r=0;r<data.rows.size();++r)for(int c=0;c<columns;++c)grid->setItem(r,c,new QTableWidgetItem(data.rows[r].value(c)));
    grid->setCurrentCell(0,0);saveButton->setEnabled(true);
    message->setText(filePath.isEmpty()?"首行为表头。应用后更新 Markdown，支持正文撤销。":"首行为表头。应用后保存到外部 CSV 文件；取消不修改文件。");
}
void TableEditorDialog::setMarkdown(const QString &source){data=readMarkdownTable(source);populate();}
void TableEditorDialog::loadCsv(const QString &path){
    filePath=path;saveButton->setEnabled(false);message->setText("正在加载完整 CSV…");
    pool.start([this,path]{QFile file(path);QByteArray bytes;TableData loaded;
        if(!file.open(QIODevice::ReadOnly))loaded.error=file.errorString();
        else if(file.size()>8*1024*1024)loaded.error="表格编辑暂支持不超过 8 MiB 的 UTF-8 CSV。";
        else{bytes=file.readAll();if(file.error()!=QFileDevice::NoError)loaded.error=file.errorString();else loaded=readCsv(bytes);}
        QMetaObject::invokeMethod(this,[this,bytes,loaded]{original=bytes;data=loaded;populate();},Qt::QueuedConnection);
    });
}
void TableEditorDialog::insertAxis(bool row,bool after){
    if(!saveButton->isEnabled())return;
    const int count=row?grid->rowCount():grid->columnCount();if(count>=(row?10000:256))return;
    if(qint64(row?count+1:grid->rowCount())*(row?grid->columnCount():count+1)>200000){message->setText("表格编辑暂支持最多 200000 个单元格。");return;}
    int at=qMax(0,row?grid->currentRow():grid->currentColumn());if(after&&count)++at;
    if(row)grid->insertRow(at);else{grid->insertColumn(at);while(data.separators.size()<count)data.separators.append("---");data.separators.insert(at,"---");}
    grid->setCurrentCell(row?at:qMax(0,grid->currentRow()),row?qMax(0,grid->currentColumn()):at);
}
void TableEditorDialog::removeAxis(bool row){
    if(!saveButton->isEnabled())return;
    if((row?grid->rowCount():grid->columnCount())<=1)return;
    int at=row?grid->currentRow():grid->currentColumn();if(at<0)return;
    if(row)grid->removeRow(at);else{grid->removeColumn(at);if(at<data.separators.size())data.separators.removeAt(at);}
}
void TableEditorDialog::moveAxis(bool row,int delta){
    if(!saveButton->isEnabled())return;
    int at=row?grid->currentRow():grid->currentColumn(),to=at+delta;
    if(at<0||to<0||to>=(row?grid->rowCount():grid->columnCount()))return;
    for(int i=0;i<(row?grid->columnCount():grid->rowCount());++i){
        auto *a=grid->takeItem(row?at:i,row?i:at),*b=grid->takeItem(row?to:i,row?i:to);
        grid->setItem(row?at:i,row?i:at,b?b:new QTableWidgetItem);grid->setItem(row?to:i,row?i:to,a?a:new QTableWidgetItem);
    }
    if(!row){while(data.separators.size()<grid->columnCount())data.separators.append("---");data.separators.swapItemsAt(at,to);}
    grid->setCurrentCell(row?to:grid->currentRow(),row?grid->currentColumn():to);
}
void TableEditorDialog::save(){
    if(!saveButton->isEnabled()||saving)return;
    saveButton->setFocus();data.rows.clear();
    for(int r=0;r<grid->rowCount();++r){QStringList row;for(int c=0;c<grid->columnCount();++c)row.append(grid->item(r,c)?grid->item(r,c)->text():QString());data.rows.append(row);}
    if(filePath.isEmpty()){const QString text=writeMarkdownTable(data);if(applyMarkdown&&!applyMarkdown(text)){message->setText("正文已发生变化，请取消后重新打开表格。");return;}emit markdownApplied(text);accept();return;}
    saving=true;setEnabled(false);const QByteArray bytes=writeCsv(data),expected=original;const QString path=filePath;
    pool.start([this,bytes,expected,path]{
        QString error;QFile current(path);
        if(!current.open(QIODevice::ReadOnly))error=current.errorString();
        else if(current.size()>8*1024*1024||current.readAll()!=expected)error="CSV 已被外部修改，请取消后重新打开，避免覆盖其他改动。";
        current.close();
        if(error.isEmpty()){QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())error=output.errorString();}
        QMetaObject::invokeMethod(this,[this,error]{saving=false;setEnabled(true);if(!error.isEmpty()){message->setText(error);return;}emit csvSaved();accept();},Qt::QueuedConnection);
    });
}
}
