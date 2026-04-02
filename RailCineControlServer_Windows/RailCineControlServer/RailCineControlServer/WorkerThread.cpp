#include "WorkerThread.h"
#include "SqlExec.h"
#include <QDebug>

void WorkerThread::run()
{
    QObject dispatcher;
    dispatcher_.store(&dispatcher, std::memory_order_release);

    // 如果 SqlExec 也是 QObject：用 deleteLater 方式托管更稳
    // 假设 SqlExec ctor: SqlExec(QObject* parent=nullptr)
    {
        SqlExec* raw = new SqlExec(&dispatcher);
        sqlExec = QSharedPointer<SqlExec>(raw, &QObject::deleteLater);
    }

    ready_.release();
    emit SigReady();

    exec();

    // 退出事件循环后 dispatcher 即将销毁
    dispatcher_.store(nullptr, std::memory_order_release);
    sqlExec.clear();                                                                // 触发 deleteLater（若事件循环已退出，deleteLater会入队但不执行；通常退出前应先清理/或直接 delete）
}

void WorkerThread::SlotPostSqlQueryTask(const QString& sql, QueryCallback cb, bool bIsAsync, const QList<QVariant>& params)
{
    if (bIsAsync)
    {
        sqlExec->executeAsyncQuery(sql, cb, params);
    }
    else
    {
        sqlExec->executeSyncQuery(sql, cb, params);
    }
}

void WorkerThread::SlotPostSqlUpdateTask(const QString& sql, UpdateCallback cb, bool bIsAsync, const QList<QVariant>& params)
{
    if (bIsAsync)
    {
        sqlExec->executeAsyncUpdate(sql, cb, params);
    }
    else
    {
        sqlExec->executeSyncUpdate(sql, cb, params);
    }
}

void WorkerThread::SlotPostSqlTransactionTask(const QList<QString>& sqls, TransactionCallback cb, bool bIsAsync, const QVariantList& allParams)
{
    if (bIsAsync) {
        sqlExec->executeAsyncTransaction(sqls, cb, allParams);
    }
    else {
        sqlExec->executeSyncTransaction(sqls, cb, allParams);
    }
}
