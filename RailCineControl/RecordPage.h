#ifndef RECORDPAGE_H
#define RECORDPAGE_H

#include <QWidget>
#include <QTableWidget>
#include <QDateEdit>

class RecordPage : public QWidget
{
    Q_OBJECT

public:
    explicit RecordPage(QWidget* parent = nullptr);                                 // 构造函数

    void AddRecordRow(const QString& date, const QString& name,                     // 专门封装的数据插入方法(对外)
        const QString& startTime, const QString& endTime,
        const QString& operatorName, const QString& endType);

private slots:
    void onSearchClicked();                                                         // 查询按钮响应
    void onExportClicked();                                                         // 导出按钮响应
    void onDeleteClicked();                                                         // 删除按钮响应

private:
    void BuildUI();                                                                 // 构建记录台UI

    // 支持传入特定日期进行加载。如果传空字符串，则加载全部。
    void LoadRecordsFromJson(const QString& targetDate);                            // 从本地加载历史记录

    void AppendRecordToJson(const QString& date, const QString& name,               // 将单条新记录追加写入Json
        const QString& startTime, const QString& endTime,
        const QString& operatorName, const QString& endType);
    void InsertRowToUI(const QString& date, const QString& name,                    // 纯UI操作：仅在表格中插入一行
        const QString& startTime, const QString& endTime,
        const QString& operatorName, const QString& endType);

    bool DeleteRecordFromJson(const QString& date, const QString& name,             // 底层 JSON 抹除
        const QString& startTime);

    QDateEdit* m_dateEdit;                                                          // 日期筛选器
    QTableWidget* m_recordTable;                                                    // 记录表格控件
};

#endif // RECORDPAGE_H