#include "RecordPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>                                                      // 导出文件需要
#include <QTextStream>                                                      // 写入文件需要
#include <QDebug>
#include <QDateTime>
#include "TCPMgr.h"

RecordPage::RecordPage(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("RecordPage");

    BuildUI();

    // ==========================================================
    // 👑 绑定网络响应槽函数 (监听服务器返回的增删查结果)
    // ==========================================================
    connect(TCPMgr::Instance().get(), &TCPMgr::SigRecordsReceived, this, &RecordPage::onGetRecordsRsp);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigRecordAdded, this, &RecordPage::onAddRecordRsp);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigRecordDeleted, this, &RecordPage::onDeleteRecordRsp);

    // 💡 启动时：默认向服务器请求加载当天的播放记录
    RequestRecordsFromServer(QDate::currentDate().toString("yyyy-MM-dd"));
}

void RecordPage::BuildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ================= 1. 筛选操作栏 =================
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(15);                                           // 全局间距

    QLabel* dateLabel = new QLabel(u8"查询日期:", this);
    dateLabel->setObjectName("recordFilterLabel");                          // 绑定 QSS

    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setCalendarPopup(true);

    QPushButton* btnSearch = new QPushButton(u8"🔍 查询", this);
    btnSearch->setObjectName("controlBtn");
    btnSearch->setMinimumSize(90, 35);

    QPushButton* btnDelete = new QPushButton(u8"🗑️ 删除", this);
    btnDelete->setObjectName("btnDeleteDanger");                            // 绑定专属危险红色 QSS
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
    m_recordTable->setFocusPolicy(Qt::NoFocus);                             // 消除焦点虚线框

    layout->addLayout(filterLayout);
    layout->addWidget(m_recordTable, 1);

    connect(btnSearch, &QPushButton::clicked, this, &RecordPage::onSearchClicked);
    connect(btnDelete, &QPushButton::clicked, this, &RecordPage::onDeleteClicked);
    connect(btnExport, &QPushButton::clicked, this, &RecordPage::onExportClicked);
}

// -------------------------------------------------------------------------
// 🚀 [查]：向服务器发送查询请求
// -------------------------------------------------------------------------
void RecordPage::RequestRecordsFromServer(const QString& targetDate)
{
    // 💡 每次发请求前，先清空 UI 表格上的旧数据
    m_recordTable->setRowCount(0);

    ServerApi::GetRecordsReq req;
    req.set_target_date(targetDate.toStdString());
    req.set_page_index(1);
    req.set_page_size(100);

    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_GET_RECORDS_REQ, req);
    qDebug() << u8"[RecordPage] 正在向云端查询播放记录，日期:" << targetDate;
}

// 接收服务器的查询响应并渲染
void RecordPage::onGetRecordsRsp(const ServerApi::GetRecordsRsp& rsp)
{
    m_recordTable->setRowCount(0); // 确保表格干净

    for (int i = 0; i < rsp.records_size(); ++i) {
        const ServerApi::PlayRecord& record = rsp.records(i);

        InsertRowToUI(
            record.record_id(), // 👑 传入服务器分配的真实数据库 ID
            QString::fromStdString(record.play_date()),
            QString::fromStdString(record.movie_name()),
            QString::fromStdString(record.start_time()),
            QString::fromStdString(record.end_time()),
            QString::fromStdString(record.operator_name()),
            QString::fromStdString(record.end_type())
        );
    }
}

// 点击查询按钮触发
void RecordPage::onSearchClicked()
{
    QString targetDate = m_dateEdit->date().toString("yyyy-MM-dd");
    RequestRecordsFromServer(targetDate);
}

// -------------------------------------------------------------------------
// 🚀 [增]：向服务器发送添加记录请求
// -------------------------------------------------------------------------
void RecordPage::RequestAddRecord(const QString& date, const QString& name, const QString& startTime, const QString& endTime, const QString& operatorName, const QString& endType)
{
    ServerApi::AddRecordReq req;
    ServerApi::PlayRecord* record = req.mutable_record();

    // 注意：不需要赋 record_id，服务器的 MySQL 会自动自增生成
    record->set_play_date(date.toStdString());
    record->set_movie_name(name.toStdString());
    record->set_start_time(startTime.toStdString());
    record->set_end_time(endTime.toStdString());
    record->set_operator_name(operatorName.toStdString());
    record->set_end_type(endType.toStdString());

    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_ADD_RECORD_REQ, req);
}

// 接收服务器的添加确认
void RecordPage::onAddRecordRsp(const ServerApi::AddRecordRsp& rsp)
{
    // 💡 添加成功后，为了拿到准确的 ID 和保持同步，直接按当前筛选日期刷新一波列表
    QString targetDate = m_dateEdit->date().toString("yyyy-MM-dd");
    RequestRecordsFromServer(targetDate);
}

// -------------------------------------------------------------------------
// 🎨 UI 辅助：纯粹负责渲染，并将云端 ID 隐藏在单元格里
// -------------------------------------------------------------------------
void RecordPage::InsertRowToUI(uint64_t recordId, const QString& date, const QString& name, const QString& startTime, const QString& endTime, const QString& operatorName, const QString& endType)
{
    m_recordTable->insertRow(0); // 始终插在最顶部

    // 创建居中Item的 Lambda
    auto createCenteredItem = [](const QString& text) -> QTableWidgetItem* {
        QTableWidgetItem* item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);                                    // 强制文本水平垂直居中
        return item;
        };

    QTableWidgetItem* dateItem = createCenteredItem(date);

    // 👑 核心绝杀：将云端主键 ID 藏在第一列的 UserRole 里，供删除时进行精确制导
    dateItem->setData(Qt::UserRole, QVariant::fromValue(recordId));

    m_recordTable->setItem(0, 0, dateItem);
    m_recordTable->setItem(0, 1, createCenteredItem(name));
    m_recordTable->setItem(0, 2, createCenteredItem(startTime));
    m_recordTable->setItem(0, 3, createCenteredItem(endTime));
    m_recordTable->setItem(0, 4, createCenteredItem(operatorName));

    QTableWidgetItem* typeItem = createCenteredItem(endType);
    if (endType == u8"强制结束") {
        typeItem->setForeground(QColor("#FF4757")); // 强制结束标红提示
    }
    m_recordTable->setItem(0, 5, typeItem);
}

// -------------------------------------------------------------------------
// 🚀 [删]：向服务器发送精确删除请求
// -------------------------------------------------------------------------
void RecordPage::onDeleteClicked()
{
    int row = m_recordTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, u8"提示", u8"请先在表格中选中要删除的记录！");
        return;
    }
    if (QMessageBox::question(this, u8"确认", u8"确定要永久删除这条播放记录吗？(云端将同步删除)") != QMessageBox::Yes) {
        return;
    }

    // 提取隐藏的真实数据库主键 ID
    uint64_t recordId = m_recordTable->item(row, 0)->data(Qt::UserRole).toULongLong();

    ServerApi::DeleteRecordReq req;
    req.set_record_id(recordId);

    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_DELETE_RECORD_REQ, req);
}

// 接收服务器的删除确认
void RecordPage::onDeleteRecordRsp(const ServerApi::DeleteRecordRsp& rsp)
{
    uint64_t deletedId = rsp.deleted_id();

    // 遍历当前表格，找到对应 ID 的那一行并在 UI 上动态移出
    for (int r = 0; r < m_recordTable->rowCount(); ++r) {
        if (m_recordTable->item(r, 0)->data(Qt::UserRole).toULongLong() == deletedId) {
            m_recordTable->removeRow(r);
            break;
        }
    }
}

// -------------------------------------------------------------------------
// 📊 导出 Excel (CSV 格式) —— 纯本地操作，直接读取表格 UI 上的内容即可
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