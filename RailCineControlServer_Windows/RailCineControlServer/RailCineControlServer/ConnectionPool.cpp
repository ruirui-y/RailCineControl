#include "ConnectionPool.h"
#include <QDebug>

ConnectionPool::ConnectionPool(QObject* parent)
    : QObject(parent)
    , m_driver(nullptr)
{
    initialize("tcp://localhost:3306", "root", "123456", "ControlHub");
}

ConnectionPool::~ConnectionPool()
{
    cleanup();
}

bool ConnectionPool::initialize(const QString& host, const QString& user, const QString& password, const QString& database)
{
    if (m_initialized) {
        return true;
    }

    m_host = host;
    m_user = user;
    m_password = password;
    m_database = database;

    // 获取 MySQL 驱动实例
    m_driver = sql::mysql::get_mysql_driver_instance();
    if (!m_driver) 
    {
        qDebug() << "Failed to get MySQL driver instance";
        return false;
    }

    // 创建连接池
    if (!createConnections(m_poolSize)) 
    {
        qDebug() << "Failed to create database connections";
        return false;
    }

    m_initialized = true;
    qDebug() << "MySQL connection pool initialized successfully with" << m_connectionPool.size() << "connections";
    return true;
}

bool ConnectionPool::createConnections(size_t poolSize)
{
    for (size_t i = 0; i < poolSize; ++i) 
    {
        auto conn = createNewConnection();
        if (conn) 
        {
            m_connectionPool.enqueue(conn);
        }
        else
        {
            qDebug() << "Failed to create database connection" << i;
        }
    }

    return !m_connectionPool.isEmpty();
}

std::shared_ptr<sql::Connection> ConnectionPool::createNewConnection()
{
    try 
    {
        // 构建连接字符串
        std::string host = m_host.toStdString();
        std::string user = m_user.toStdString();
        std::string password = m_password.toStdString();

        // 创建连接
        sql::Connection* rawConn = m_driver->connect(host, user, password);
        if (!rawConn) {
            qDebug() << "Failed to create raw connection";
            return nullptr;
        }

        // 设置数据库
        rawConn->setSchema(m_database.toStdString());

        // 设置连接选项
        rawConn->setClientOption("characterSetResults", "utf8");
        rawConn->setClientOption("charset", "utf8");

        // 包装为 shared_ptr 并设置自定义删除器
        auto conn = std::shared_ptr<sql::Connection>(rawConn,
            [](sql::Connection* conn) {
                if (conn) {
                    conn->close();
                    delete conn;
                }
            });

        // 测试连接是否有效
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));

        return conn;

    }
    catch (const sql::SQLException& e) {
        qDebug() << "MySQL error creating connection:"
            << e.what() << ", MySQL error code:" << e.getErrorCode()
            << ", SQLState:" << e.getSQLState().c_str();
        return nullptr;
    }
    catch (const std::exception& e) {
        qDebug() << "Error creating connection:" << e.what();
        return nullptr;
    }
}

std::shared_ptr<sql::Connection> ConnectionPool::getConnection()
{
    if (m_connectionPool.isEmpty()) {
        qDebug() << "Connection pool is empty, creating new connection";
        return createNewConnection();
    }

    auto conn = m_connectionPool.dequeue();

    // 检查连接是否仍然有效
    try
    {
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
        return conn;
    }
    catch (const sql::SQLException& e)
    {
        qDebug() << "Connection is invalid, creating new one:" << e.what();
        return createNewConnection();
    }
    catch (const std::exception& e)
    {
        qDebug() << "Connection check failed, creating new one:" << e.what();
        return createNewConnection();
    }
}

void ConnectionPool::returnConnection(std::shared_ptr<sql::Connection> conn)
{
    if (!conn) {
        return;
    }

    // 检查连接是否仍然有效
    try {
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));

        // 如果池子未满，归还连接
        if (m_connectionPool.size() < m_poolSize) {
            m_connectionPool.enqueue(conn);
        }
        else {
            // 池子已满，关闭连接（通过 shared_ptr 的删除器自动处理）
        }
    }
    catch (const sql::SQLException& e) {
        qDebug() << "Returned connection is invalid, closing:" << e.what();
        // 连接会自动关闭，因为我们在创建时设置了删除器
    }
    catch (const std::exception& e) {
        qDebug() << "Connection check failed on return, closing:" << e.what();
    }
}

void ConnectionPool::cleanup()
{
    // 清空连接池，连接会自动关闭
    m_connectionPool.clear();
    m_initialized = false;
}