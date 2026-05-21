#ifndef GLOBAL_H
#define GLOBAL_H

#include <functional>
#include <memory>
#include <iostream>
#include <mutex>
#include <QSettings>
#include <vector>
#include <QDebug>
#include <atomic>
#include <QElapsedTimer>
#include <qelapsedtimer.h>
#include <QSharedPointer>
#include "ConnectionPool.h"
#include "singletion.h"

using DeferFunc = std::function<void()>;
class Defer
{
public:
    Defer(DeferFunc func) : m_func(func) {}
    ~Defer() { qDebug() << "~Defer"; m_func(); }
private:
    DeferFunc m_func;
};

// 查询回调函数
using QueryCallback = std::function<void(const QList<QVariantMap>&)>;

// Update回调函数
using UpdateCallback = std::function<void(int)>;

// 事务回调函数
using TransactionCallback = std::function<void(bool)>;


namespace sql { class Connection; }

// ==========================================
// 航母级 RAII 连接护盾 (终极极简版)
// ==========================================
class ConnectionGuard {
public:
    // 构造时：接收裸指针（借用身份），绝不触发智能指针的拷贝！
    ConnectionGuard(ConnectionPool* pool, std::shared_ptr<sql::Connection> conn)
        : m_pool(pool), m_conn(conn) {
    }

    // 析构时：自动归还连接
    ~ConnectionGuard() {
        if (m_pool && m_conn) {
            m_pool->returnConnection(m_conn);
        }
    }

    // 极客防御：彻底封死拷贝！
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    ConnectionPool* m_pool;
    std::shared_ptr<sql::Connection> m_conn;
};

// ==========================================
// 🚀 全局配置管理中枢 (GlobalConfig)
// ==========================================
class GlobalConfig : public Singleton<GlobalConfig>
{
public:
    friend class Singleton<GlobalConfig>;

public:
    // 在 QApp 初始化后调用，传入 qApp->applicationDirPath()
    bool Init(const QString& appDirPath);

    // ================= 极速读取接口 =================
    int GetTcpPort() const { return m_tcpPort; }
    int GetHttpPort() const { return m_httpPort; }

    QString GetWxNotifyUrl() const { return m_wxNotifyUrl; }

    // ================= 👑 数据库读取接口 =================
    QString GetDbHost() const { return m_dbHost; }
    int     GetDbPort() const { return m_dbPort; }
    QString GetDbUser() const { return m_dbUser; }
    QString GetDbPwd() const { return m_dbPwd; }
    QString GetDbName() const { return m_dbName; }

    // 自动拼接形如 "tcp://localhost:3306" 的连接字符串
    QString GetDbUrl() const { return QString("tcp://%1:%2").arg(m_dbHost).arg(m_dbPort); }

    QString GetConfigPath() const { return m_configPath; }

private:
    GlobalConfig() = default;

    QString m_configPath;

    // 服务器配置缓存
    int m_tcpPort = 5486;
    int m_httpPort = 8182;

    // 微信支付配置缓存
    QString m_wxNotifyUrl;

    // 数据库配置缓存 (带兜底默认值)
    QString m_dbHost = "localhost";
    int     m_dbPort = 3306;
    QString m_dbUser = "root";
    QString m_dbPwd = "123456";
    QString m_dbName = "ControlHub";
};

#endif // GLOBAL_H
