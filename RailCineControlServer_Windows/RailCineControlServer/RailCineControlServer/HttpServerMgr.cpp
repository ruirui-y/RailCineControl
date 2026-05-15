#include "HttpServerMgr.h"
#include <QDebug>

// 👑 极其重要：在包含 httplib 之前，必须定义这个宏，它才会启用 OpenSSL 支持！
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

// 你后续需要自己写的微信 V3 验签和解密工具类
// #include "WechatPayCrypto.h" 

HttpServerMgr::HttpServerMgr(QObject* parent) : QObject(parent), m_isRunning(false) {
    m_server = std::make_unique<httplib::Server>();
}

HttpServerMgr::~HttpServerMgr() {
    Stop();
}

void HttpServerMgr::Start(int port)
{
    if (m_isRunning) return;

    // ---------------------------------------------------------
    // 👑 注册微信支付的专属 Webhook 路由
    // ---------------------------------------------------------
    m_server->Post("/api/wechat/pay_notify", [this](const httplib::Request& req, httplib::Response& res) {
        qDebug() << u8"[HttpServer] 收到微信支付回调请求！";

        // 1. 获取微信发来的 Header (用于 RSA 验签)
        auto sig = req.get_header_value("Wechatpay-Signature");
        auto timestamp = req.get_header_value("Wechatpay-Timestamp");
        auto nonce = req.get_header_value("Wechatpay-Nonce");
        auto serial = req.get_header_value("Wechatpay-Serial");

        // 2. 获取加密的 Body 数据
        std::string body = req.body;
        qDebug() << u8"[HttpServer] 回调密文:" << QString::fromStdString(body);

        // TODO: 3. 调用 OpenSSL 进行验签 (验证这个请求真的是微信发的)
        // bool isValid = WechatPayCrypto::VerifySignature(...);
        // if(!isValid) { res.status = 401; return; }

        // TODO: 4. 调用 OpenSSL 的 AEAD_AES_256_GCM 解密 body，得到明文 JSON
        // std::string plainJson = WechatPayCrypto::Decrypt(body, ...);

        // 假设解密成功，解析 JSON 拿到了你本地的订单号
        QString out_trade_no = "PAY20260515_xxx"; // 从 JSON 里提出来的
        QString transaction_id = "42000000000000000000"; // 微信的真实流水号

        // 5. 抛出信号，让 Qt 主线程/数据库线程去处理状态流转
        // 使用 QMetaObject::invokeMethod 或者 QueuedConnection 确保跨线程安全
        emit SigWechatPaySuccess(out_trade_no, transaction_id);

        // 6. 给微信返回 200 OK，否则微信会疯狂重发回调
        res.status = 200;
        res.set_content("{\"code\": \"SUCCESS\", \"message\": \"OK\"}", "application/json");
        });

    // ---------------------------------------------------------
    // 将 listen 扔进后台线程，绝不阻塞 Qt
    // ---------------------------------------------------------
    m_isRunning = true;
    m_listenThread = std::thread([this, port]() {
        qDebug() << u8"[HttpServer] 开始监听微信回调，端口:" << port;
        // 这是一句死循环阻塞代码，直到调用 m_server->stop() 才会退出
        m_server->listen("0.0.0.0", port);
        qDebug() << u8"[HttpServer] 监听已停止";
        });
}

void HttpServerMgr::Stop()
{
    if (m_isRunning) {
        m_server->stop();
        if (m_listenThread.joinable()) {
            m_listenThread.join();
        }
        m_isRunning = false;
    }
}