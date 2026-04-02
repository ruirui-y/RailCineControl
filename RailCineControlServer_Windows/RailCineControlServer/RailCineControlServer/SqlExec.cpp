#include "SqlExec.h"
#include <QThread>

#include "ThreadPool.h"

SqlExec::SqlExec(QObject* parent)
{
    m_mysqlPool = std::make_unique<ConnectionPool>();
}

SqlExec::~SqlExec()
{
    // 工作线程会在对象销毁时自动停止
}

void SqlExec::executeAsyncQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params)
{
    QMetaObject::invokeMethod(this, "processQuery", Qt::QueuedConnection,
        Q_ARG(QString, sql),
        Q_ARG(QueryCallback, query),
        Q_ARG(QList<QVariant>, params));
}

void SqlExec::executeAsyncUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params)
{
    QMetaObject::invokeMethod(this, "processUpdate", Qt::QueuedConnection,
        Q_ARG(QString, sql),
        Q_ARG(UpdateCallback, update),
        Q_ARG(QList<QVariant>, params));
}

void SqlExec::executeAsyncTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams)
{
    QMetaObject::invokeMethod(this, "processTransaction", Qt::QueuedConnection,
        Q_ARG(QList<QString>, sqls),
        Q_ARG(TransactionCallback, cb),
        Q_ARG(QVariantList, allParams));
}

void SqlExec::executeSyncQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params)
{
    QMetaObject::invokeMethod(this, "processQuery", Qt::BlockingQueuedConnection,
        Q_ARG(QString, sql),
        Q_ARG(QueryCallback, query),
        Q_ARG(QList<QVariant>, params));
}

void SqlExec::executeSyncUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params)
{
    QMetaObject::invokeMethod(this, "processUpdate", Qt::BlockingQueuedConnection,
        Q_ARG(QString, sql),
        Q_ARG(UpdateCallback, update),
        Q_ARG(QList<QVariant>, params));
}

void SqlExec::executeSyncTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams)
{
    QMetaObject::invokeMethod(this, "processTransaction", Qt::BlockingQueuedConnection,
        Q_ARG(QList<QString>, sqls),
        Q_ARG(TransactionCallback, cb),
        Q_ARG(QVariantList, allParams));
}

void SqlExec::processQuery(const QString& sql, QueryCallback query, const QList<QVariant>& params)
{
    // 1. 获取连接
    auto conn = m_mysqlPool->getConnection();
    if (!conn)
    {
        qDebug() << "Failed to get database connection from pool";
        // 异常兜底：回调不能丢！丢个空列表回去，防止上层死等
        if (query) query(QList<QVariantMap>());
        return;
    }

    // 2. RAII 连接护盾：无论发生什么，离开作用域自动归还连接！
    ConnectionGuard guard(m_mysqlPool.get(), conn);

    // 准备存放最终结果
    QList<QVariantMap> results;

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        std::unique_ptr<sql::ResultSet> res;

        if (params.isEmpty())
        {
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            res.reset(stmt->executeQuery(sql.toStdString()));
        }
        else
        {
            pstmt.reset(conn->prepareStatement(sql.toStdString()));
            for (int i = 0; i < params.size(); ++i)
            {
                const QVariant& param = params[i];
                if (param.isNull()) {
                    pstmt->setNull(i + 1, 0);
                }
                else {
                    bindValue(pstmt.get(), i + 1, param);
                }
            }
            res.reset(pstmt->executeQuery());
        }

        // 获取列信息
        sql::ResultSetMetaData* meta = res->getMetaData();
        int columnCount = meta->getColumnCount();

        // 3. 🔴 性能核武：提前缓存列名，彻底消灭循环内的无意义深拷贝！
        std::vector<std::string> stdColNames(columnCount);
        std::vector<QString> qColNames(columnCount);
        for (int i = 0; i < columnCount; ++i) {
            stdColNames[i] = meta->getColumnLabel(i + 1);
            qColNames[i] = QString::fromStdString(stdColNames[i]);
        }

        // 4. 高速组装数据
        // 便利每一行
        while (res->next())
        {
            QVariantMap record;
            // 获取每一列对应的键值
            for (int i = 0; i < columnCount; ++i)                           // 注意缓存数组是从 0 开始的
            {
                if (!res->isNull(i + 1))                                    // ResultSet 的索引是从 1 开始的
                {
                    // 传入 std::string，作为 key 使用已缓存好的 QString
                    QVariant value = praseResult(res.get(), stdColNames[i]);
                    record[qColNames[i]] = value;
                }
            }

            results.append(record);                                         // 存储每一行的详细数据
        }
    }
    catch (const sql::SQLException& e) 
    {
        qDebug() << "MySQL error executing query:" << e.what()
            << ", Error code:" << e.getErrorCode()
            << ", SQLState:" << e.getSQLState().c_str();
        // 注意：纯 SELECT 查询通常不需要 rollback，直接记录日志即可
    }
    catch (const std::exception& e)
    {
        qDebug() << "Error executing query:" << e.what();
    }

    // 5. 🔴 绝对安全的回调触发点
    // 无论是正常查出数据，还是 try 块中途崩溃跳入 catch，最终都会执行到这里。
    // 如果抛异常了，results 就是空的，上层业务拿到空列表也能正确处理，绝不卡死。
    if (query)
    {
        query(results);
    }

    // 退出函数时，ConnectionGuard 会自动触发析构，将 conn 归还连接池！
}

void SqlExec::processUpdate(const QString& sql, UpdateCallback update, const QList<QVariant>& params)
{
    auto conn = m_mysqlPool->getConnection();
    if (!conn) {
        qDebug() << "Failed to get database connection from pool";
        // 兜底：返回 -1 代表执行异常，区分于 0 (执行成功但无行被修改)
        if (update) update(-1);
        return;
    }

    // 核心护盾：绝对保证连接归还
    ConnectionGuard guard(m_mysqlPool.get(), conn);

    int affectedRows = -1;

    try {
        if (params.isEmpty()) {
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            affectedRows = stmt->executeUpdate(sql.toStdString());
        }
        else {
            std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql.toStdString()));
            for (int i = 0; i < params.size(); ++i) {
                const QVariant& param = params[i];
                if (param.isNull()) {
                    pstmt->setNull(i + 1, 0);
                }
                else {
                    bindValue(pstmt.get(), i + 1, param);
                }
            }
            affectedRows = pstmt->executeUpdate();
        }
        // qDebug() << "Affected rows:" << affectedRows;
    }
    catch (const sql::SQLException& e) {
        qDebug() << "MySQL error executing update:" << e.what()
            << ", Error code:" << e.getErrorCode()
            << ", SQLState:" << e.getSQLState().c_str();
        // 普通 Update 失败不需要 rollback
    }
    catch (const std::exception& e) {
        qDebug() << "Error executing update:" << e.what();
    }

    // 绝对触发区：无论 try 还是 catch，必定执行回调唤醒上层！
    if (update) {
        update(affectedRows);
    }
}

// 将多个sql的执行，封装成原子操作，要么全部成功，要么全部失败
void SqlExec::processTransaction(const QList<QString>& sqls, TransactionCallback cb, const QVariantList& allParams)
{
    auto conn = m_mysqlPool->getConnection();
    if (!conn) {
        qDebug() << "Transaction failed: Cannot get database connection from pool";
        if (cb) cb(false);
        return;
    }

    // 核心护盾
    ConnectionGuard guard(m_mysqlPool.get(), conn);

    bool bSuccess = false;

    try {
        // 1. 开启事务
        conn->setAutoCommit(false);

        // 2. 批量执行
        for (int i = 0; i < sqls.size(); ++i) {
            const QString& sql = sqls[i];
            QList<QVariant> current_params;

            if (i < allParams.size()) {
                current_params = allParams[i].toList();
            }

            if (current_params.isEmpty()) {
                std::unique_ptr<sql::Statement> stmt(conn->createStatement());
                stmt->executeUpdate(sql.toStdString());
            }
            else {
                std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql.toStdString()));
                for (int j = 0; j < current_params.size(); ++j) {
                    const QVariant& param = current_params[j];
                    if (param.isNull()) {
                        pstmt->setNull(j + 1, 0);
                    }
                    else {
                        bindValue(pstmt.get(), j + 1, param);
                    }
                }
                pstmt->executeUpdate();
            }
        }

        // 3. 提交事务
        conn->commit();
        bSuccess = true;
        qDebug() << "Transaction committed successfully. Executed SQL count:" << sqls.size();

    }
    catch (const sql::SQLException& e) {
        qDebug() << "MySQL Transaction Error:" << e.what() << ", SQLState:" << e.getSQLState().c_str();
        // 🔴 极客防御：防止 rollback 自身抛出异常导致护盾穿透
        try { conn->rollback(); }
        catch (...) { qDebug() << "Rollback failed!"; }
    }
    catch (const std::exception& e) {
        qDebug() << "Transaction Error:" << e.what();
        try { conn->rollback(); }
        catch (...) { qDebug() << "Rollback failed!"; }
    }

    // 4. 🔴 环境复原：无论成功还是异常，必须恢复连接的自动提交状态！
    // 否则这个连接回到连接池后，会变成一个“毒连接”，后续拿它的线程都会莫名其妙开事务！
    try {
        conn->setAutoCommit(true);
    }
    catch (...) {
        qDebug() << "Failed to restore auto-commit state.";
        // 极端情况下如果连 setAutoCommit 都失败了，这个连接其实已经废了。
        // MySQL Connector/C++ 的连接池一般会有 validate 机制，交给底层处理即可。
    }

    // 5. 绝对触发区
    if (cb) {
        cb(bSuccess);
    }
}

void SqlExec::bindValue(sql::PreparedStatement* pstmt, int index, const QVariant& value)
{
    if (value.isNull()) {
        pstmt->setNull(index, 0);
        return;
    }

    // 根据 QVariant 类型调用对应的绑定方法
    switch (value.type()) {
    case QVariant::Int:
        pstmt->setInt(index, value.toInt());
        break;
    case QVariant::Bool:
        pstmt->setBoolean(index, value.toBool());
        break;
    case QVariant::String:
    default:  // 其他类型（如 QString）按字符串处理
        pstmt->setString(index, value.toString().toStdString());
    }
}

QVariant SqlExec::praseResult(sql::ResultSet* result, const std::string& columnName)
{
    if (result->isNull(columnName))
    {
        return QVariant();
    }

    sql::ResultSetMetaData* meta = result->getMetaData();
    int columnType = meta->getColumnType(result->findColumn(columnName));

    switch (columnType) {
    case sql::DataType::INTEGER:
        return QVariant(result->getInt(columnName));
    case sql::DataType::BIT:
        return QVariant(result->getBoolean(columnName));
    case sql::DataType::VARCHAR:
    default:
        return QVariant(QString::fromStdString(result->getString(columnName)));
    }
}