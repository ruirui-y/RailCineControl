#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include "TcpServer.h"

class OrderManagementService : public QObject, public std::enable_shared_from_this<OrderManagementService>
{
    Q_OBJECT

public:
    OrderManagementService(TcpServer& server, QObject* parent = nullptr)
        : QObject(parent), m_server(server) {
    }
    ~OrderManagementService();

    void Init();

private slots:
    void CheckUnpaidOrders(); // 必须是 slots

private:
    TcpServer& m_server;
    QTimer* m_timer;
};