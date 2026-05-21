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
    void onSessionClosed(qintptr fd);

    // 当用户登录成功
    void OnUserLoginSuccess(qintptr fd, uint64_t userId);

    // 处理微信支付结果
    void OnPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status);

private:
    void StartCleanupUnpaidTask();                                                                  // 启动清理未支付订单的定时任务

private:
    // 1. 物理层 Map (句柄 -> 会话) 
    // 作用：负责底层网络事件、断线清理、防恶意连接
    QMap<qintptr, std::shared_ptr<ClientSession>> m_fdSessions;

    // 2. 业务层 Map (用户ID -> 会话) 
    // 作用：负责业务推送、支付成功通知、顶号踢人机制
    QMap<uint64_t, std::shared_ptr<ClientSession>> m_userSessions;

    QTimer* m_cleanupUnpaidTaskTimer;                                                               // 清理未支付订单的定时任务
};