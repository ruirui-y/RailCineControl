#pragma once
#include <QObject>
#include <thread>
#include <atomic>
#include <memory>

namespace httplib { class Server; }

class HttpServerMgr : public QObject
{
    Q_OBJECT

public:
    HttpServerMgr(QObject* parent = nullptr);
    ~HttpServerMgr();

public:
    void Start(int port);
    void Stop();

signals:
    // 统一的支付结果通知信号
    void SigPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status);

private:
    std::unique_ptr<httplib::Server> m_server;
    std::atomic<bool> m_isRunning;
};