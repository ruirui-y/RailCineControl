#pragma once
#include <QObject>
#include <thread>
#include <atomic>

// 提前声明，避免引入巨大的 httplib.h 拖慢编译
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
    // 当收到微信回调，并且验签、解密都成功后，抛出这个信号！
    // 你的 TCPMgr 收到这个信号，就可以去查库找 TCP 会话并下发推送了
    void SigWechatPaySuccess(const QString& out_trade_no, const QString& transaction_id);

private:
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_listenThread;
    std::atomic<bool> m_isRunning;
};