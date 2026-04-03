#include "RecordPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QFileDialog>                                                              // 💡 导出文件需要
#include <QTextStream>                                                              // 💡 写入文件需要
#include <QDebug>

#include "JsonTool.h"
extern QString MovieRecordPath;

RecordPage::RecordPage(QWidget* parent) : QWidget(parent)
{
    BuildUI();
    // 💡 启动时：默认加载当天的播放记录
    LoadRecordsFromJson(QDate::currentDate().toString("yyyy-MM-dd"));
}

void RecordPage::BuildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ================= 1. 筛选操作栏 =================
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(15);                                               // 💡 全局间距

    QLabel* dateLabel = new QLabel(u8"查询日期:", this);
    dateLabel->setObjectName("recordFilterLabel");                              // 👑 绑定 QSS

    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setCalendarPopup(true);

    QPushButton* btnSearch = new QPushButton(u8"🔍 查询", this);
    btnSearch->setObjectName("controlBtn");
    btnSearch->setMinimumSize(90, 35);

    QPushButton* btnDelete = new QPushButton(u8"🗑️ 删除", this);
    btnDelete->setObjectName("btnDeleteDanger");                                // 👑 绑定专属危险红色 QSS
    btnDelete->setMinimumSize(90, 35);

    QPushButton* btnExport = new QPushButton(u8"📊 导出 Excel", this);
    btnExport->setObjectName("controlBtn");
    btnExport->setMinimumSize(120, 35);

    filterLayout->addWidget(dateLabel);
    filterLayout->addWidget(m_dateEdit);
    filterLayout->addWidget(btnSearch);
    filterLayout->addWidget(btnDelete);
    filterLayout->addStretch();
    filterLayout->addWidget(btnExport);

    // ================= 2. 数据表格 =================
    m_recordTable = new QTableWidget(0, 6, this);
    m_recordTable->setHorizontalHeaderLabels({
        u8"播放日期", u8"影片名称", u8"开始时间", u8"结束时间", u8"操作员", u8"结束类型"
        });

    m_recordTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_recordTable->setAlternatingRowColors(true);
    m_recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recordTable->verticalHeader()->setVisible(false);
    m_recordTable->setFocusPolicy(Qt::NoFocus);                                 // 消除焦点虚线框

    layout->addLayout(filterLayout);
    layout->addWidget(m_recordTable, 1);

    connect(btnSearch, &QPushButton::clicked, this, &RecordPage::onSearchClicked);
    connect(btnDelete, &QPushButton::clicked, this, &RecordPage::onDeleteClicked);
    connect(btnExport, &QPushButton::clicked, this, &RecordPage::onExportClicked);
}
// -------------------------------------------------------------------------
// 核心加载引擎：支持按日期精准过滤
// -------------------------------------------------------------------------
void RecordPage::LoadRecordsFromJson(const QString& targetDate)
{
    // 💡 每次加载前，先清空 UI 表格上的旧数据
    m_recordTable->setRowCount(0);

    QFileInfo fileInfo(MovieRecordPath);
    if (!fileInfo.exists()) {
        QJsonDocument initialDoc(QJsonArray{});
        QString createErr;
        JsonTool::Instance()->writeJsonFile(MovieRecordPath, initialDoc, &createErr);
        return;
    }

    QJsonDocument doc;
    QString errMsg;
    if (JsonTool::Instance()->readJsonFile(MovieRecordPath, doc, &errMsg) && doc.isArray()) {
        QJsonArray recordArray = doc.array();

        for (int i = 0; i < recordArray.size(); ++i) {
            QJsonObject obj = recordArray[i].toObject();
            QString recordDate = obj["date"].toString();

            // 👑 过滤引擎：如果传了 targetDate，就只加载那一天的记录
            if (targetDate.isEmpty() || recordDate == targetDate) {
                InsertRowToUI(
                    recordDate,
                    obj["name"].toString(),
                    obj["startTime"].toString(),
                    obj["endTime"].toString(),
                    obj["operatorName"].toString(),
                    obj["endType"].toString()
                );
            }
        }
    }
}

void RecordPage::AddRecordRow(const QString& date, const QString& name, const QString& startTime, const QString& endTime, const QString& operatorName, const QString& endType) {
    InsertRowToUI(date, name, startTime, endTime, operatorName, endType);
    AppendRecordToJson(date, name, startTime, endTime, operatorName, endType);
}

void RecordPage::InsertRowToUI(const QString& date, const QString& name, const QString& startTime, const QString& endTime, const QString& operatorName, const QString& endType) {
    m_recordTable->insertRow(0);
    
    // 创建居中Item
    auto createCenteredItem = [](const QString& text) -> QTableWidgetItem* {
        QTableWidgetItem* item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);                                    // 强制文本水平垂直居中
        return item;
        };

    m_recordTable->setItem(0, 0, createCenteredItem(date));
    m_recordTable->setItem(0, 1, createCenteredItem(name));
    m_recordTable->setItem(0, 2, createCenteredItem(startTime));
    m_recordTable->setItem(0, 3, createCenteredItem(endTime));
    m_recordTable->setItem(0, 4, createCenteredItem(operatorName));
    QTableWidgetItem* typeItem = createCenteredItem(endType);
    if (endType == u8"强制结束") typeItem->setForeground(QColor("#FF4757"));
    m_recordTable->setItem(0, 5, typeItem);
}

void RecordPage::AppendRecordToJson(const QString& date, const QString& name, const QString& startTime, const QString& endTime, const QString& operatorName, const QString& endType) {
    QJsonDocument doc; QString errMsg;
    JsonTool::Instance()->readJsonFile(MovieRecordPath, doc, &errMsg);
    QJsonArray recordArray = doc.isArray() ? doc.array() : QJsonArray();
    QJsonObject newRecord;
    newRecord["date"] = date; newRecord["name"] = name; newRecord["startTime"] = startTime;
    newRecord["endTime"] = endTime; newRecord["operatorName"] = operatorName; newRecord["endType"] = endType;
    recordArray.append(newRecord);
    doc.setArray(recordArray);
    JsonTool::Instance()->writeJsonFile(MovieRecordPath, doc, &errMsg);
}

bool RecordPage::DeleteRecordFromJson(const QString& date, const QString& name, const QString& startTime) {
    QJsonDocument doc; QString errMsg;
    if (!JsonTool::Instance()->readJsonFile(MovieRecordPath, doc, &errMsg)) return false;
    if (!doc.isArray()) return false;
    QJsonArray recordArray = doc.array();
    bool found = false;
    for (int i = 0; i < recordArray.size(); ++i) {
        QJsonObject obj = recordArray[i].toObject();
        if (obj["date"].toString() == date && obj["name"].toString() == name && obj["startTime"].toString() == startTime) {
            recordArray.removeAt(i); found = true; break;
        }
    }
    if (found) {
        doc.setArray(recordArray);
        return JsonTool::Instance()->writeJsonFile(MovieRecordPath, doc, &errMsg);
    }
    return false;
}

void RecordPage::onDeleteClicked() {
    int row = m_recordTable->currentRow();
    if (row < 0) { QMessageBox::warning(this, u8"提示", u8"请先在表格中选中要删除的记录！"); return; }
    if (QMessageBox::question(this, u8"确认", u8"确定要永久删除这条播放记录吗？") != QMessageBox::Yes) return;
    QString date = m_recordTable->item(row, 0)->text();
    QString name = m_recordTable->item(row, 1)->text();
    QString startTime = m_recordTable->item(row, 2)->text();
    if (DeleteRecordFromJson(date, name, startTime)) {
        m_recordTable->removeRow(row);
    }
}

// -------------------------------------------------------------------------
// 💡 完善：查询功能
// -------------------------------------------------------------------------
void RecordPage::onSearchClicked()
{
    QString targetDate = m_dateEdit->date().toString("yyyy-MM-dd");
    qDebug() << u8"执行按日期查询:" << targetDate;

    // 直接复用加载引擎，传入选中的日期重新渲染表格
    LoadRecordsFromJson(targetDate);
}

// -------------------------------------------------------------------------
// 💡 完善：导出 Excel (CSV 格式)
// -------------------------------------------------------------------------
void RecordPage::onExportClicked()
{
    // 如果表格是空的，直接驳回
    if (m_recordTable->rowCount() == 0) {
        QMessageBox::information(this, u8"提示", u8"当前没有可导出的数据！");
        return;
    }

    // 默认保存文件名加上当天的日期
    QString defaultFileName = QString("MovieRecords_%1.csv").arg(QDate::currentDate().toString("yyyyMMdd"));

    QString filePath = QFileDialog::getSaveFileName(this, u8"导出播放记录", defaultFileName, u8"CSV 文件 (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, u8"错误", u8"文件创建失败，请检查文件是否被占用！");
        return;
    }

    QTextStream out(&file);

    // 1. 写入表头
    QStringList headers;
    for (int c = 0; c < m_recordTable->columnCount(); ++c) {
        headers << m_recordTable->horizontalHeaderItem(c)->text();
    }
    out << headers.join(",") << "\n";

    // 2. 遍历写入数据行
    for (int r = 0; r < m_recordTable->rowCount(); ++r) {
        QStringList rowData;
        for (int c = 0; c < m_recordTable->columnCount(); ++c) {
            // 拿到单元格文本，如果有英文逗号，CSV 需要用双引号包裹防错乱
            QString cellText = m_recordTable->item(r, c)->text();
            if (cellText.contains(",")) {
                cellText = "\"" + cellText + "\"";
            }
            rowData << cellText;
        }
        out << rowData.join(",") << "\n";
    }

    file.close();
    QMessageBox::information(this, u8"成功", u8"播放记录已成功导出！");
}