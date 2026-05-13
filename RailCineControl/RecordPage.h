#ifndef RECORDPAGE_H
#define RECORDPAGE_H

#include <QWidget>
#include <QTableWidget>
#include <QDateEdit>
#include "common.pb.h"          
#include "server_msg.pb.h"      

class RecordPage : public QWidget
{
    Q_OBJECT

public:
    explicit RecordPage(QWidget* parent = nullptr);                                 // 构造函数

    // 专门封装的数据插入方法(供外部 PlaybackPage 播放结束时调用)
    // 这里不再直接操作 UI 或 JSON，而是直接发请求给服务器
    void RequestAddRecord(const QString& date, const QString& name,
        const QString& startTime, const QString& endTime,
        const QString& operatorName, const QString& endType);

private slots:
    // ================= UI 按钮交互 =================
    void onSearchClicked();                                                         // 查询按钮响应
    void onExportClicked();                                                         // 导出按钮响应
    void onDeleteClicked();                                                         // 删除按钮响应

    // ================= 网络数据回执 =================
    // 处理 TCPMgr 转发过来的服务端异步响应
    void onGetRecordsRsp(const ServerApi::GetRecordsRsp& rsp);                      // 接收查询列表
    void onAddRecordRsp(const ServerApi::AddRecordRsp& rsp);                        // 接收添加成功确认
    void onDeleteRecordRsp(const ServerApi::DeleteRecordRsp& rsp);                  // 接收删除成功确认

private:
    void BuildUI();                                                                 // 构建记录台UI

    // 向服务器索要特定日期的记录
    void RequestRecordsFromServer(const QString& targetDate);

    // 仅负责在表格中画出一行，并将云端主键 ID 藏入单元格
    void InsertRowToUI(uint64_t recordId, const QString& date, const QString& name,
        const QString& startTime, const QString& endTime,
        const QString& operatorName, const QString& endType);

private:
    QDateEdit* m_dateEdit;                                                          // 日期筛选器
    QTableWidget* m_recordTable;                                                    // 记录表格控件
};

#endif // RECORDPAGE_H