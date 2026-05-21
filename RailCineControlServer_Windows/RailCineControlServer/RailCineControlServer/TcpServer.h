#pragma once
#include <QTcpServer>
#include <QThread>
#include <QMap>
#include <memory>
#include <QTimer>
#include "ClientSession.h"

class TcpServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit TcpServer(QObject* parent = nullptr);
    ~TcpServer();

    bool StartServer(quint16 port);

protected:
    // 核心破局点：重写底层的接入函数
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    // 清理断开连接的客户端
    void onSessionClosed(ClientSession* session);

    // 处理微信支付结果
    void OnPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status);

private:
    int GetOnlineCount() const;
    void StartCleanupUnpaidTask();                                                                  // 启动清理未支付订单的定时任务

private:
    // 用智能指针管理所有存活的客户端！
    // Key 可以是句柄，也可以是你自定义的会话 ID
    QMap<qintptr, std::shared_ptr<ClientSession>> m_sessions;
    QTimer* m_cleanupUnpaidTaskTimer;                                                               // 清理未支付订单的定时任务
};