#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H
#include <QObject>
#include <memory>
#include <mysql/jdbc.h>
#include <QQueue>

class ConnectionPool : public QObject
{
    Q_OBJECT

public:
    std::shared_ptr<sql::Connection> getConnection();
    void returnConnection(std::shared_ptr<sql::Connection> conn);

    void cleanup();

public:
    ConnectionPool(QObject* parent = nullptr);
    ~ConnectionPool();

private:
    bool initialize(const QString& host, const QString& user, const QString& password, const QString& database);
    bool createConnections(size_t poolSize);
    std::shared_ptr<sql::Connection> createNewConnection();

private:
    QString m_host;
    QString m_user;
    QString m_password;
    QString m_database;

    sql::Driver* m_driver;
    QQueue<std::shared_ptr<sql::Connection>> m_connectionPool;
    bool m_initialized = false;
    size_t m_poolSize = 5;
};
#endif // CONNECTIONPOOL_H