#ifndef SQL_EXEC_H
#define SQL_EXEC_H
#include "Global.h"
#include "ConnectionPool.h"

class QThread;
class SqlExec : public QObject
{
    Q_OBJECT

public:
    explicit SqlExec(QObject* parent = nullptr);
    ~SqlExec();

public:
    // 异步查询（SELECT），返回结果集
    void executeAsyncQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params = QList<QVariant>());
    // 异步执行更新
    void executeAsyncUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params = QList<QVariant>());

    // 异步执行事务
    // allParams 里面装的是每一个 SQL 对应的参数列表 (即 QVariantList 内部套 QVariantList)
    void executeAsyncTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams = QVariantList());

    // 同步操作
    void executeSyncQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params = QList<QVariant>());
    void executeSyncUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params = QList<QVariant>());
    void executeSyncTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams = QVariantList());

private slots:
    void processQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params);
    void processUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params);
    void processTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams);

private:
    void bindValue(sql::PreparedStatement* pstmt, int index, const QVariant& value);
    QVariant praseResult(sql::ResultSet* result, const std::string& columnName);

private:
    std::unique_ptr<ConnectionPool> m_mysqlPool;
};
#endif // SQL_EXEC_H