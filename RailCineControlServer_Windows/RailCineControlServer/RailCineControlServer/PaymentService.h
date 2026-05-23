#pragma once
#include <QObject>
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

// 前向声明
class ClientSession;
class TcpServer;

class PaymentService : public QObject, public std::enable_shared_from_this<PaymentService>
{
    Q_OBJECT

public:
    PaymentService(TcpServer& server, QObject* parent = nullptr)
        : QObject(parent), m_server(server) {
    }
    ~PaymentService();

    void Init();

public slots:
    // 处理支付结果
    void ProcessPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status);

private:
    // 获取支付二维码
    void OnCreateOrder(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);

private:
    TcpServer& m_server;
};