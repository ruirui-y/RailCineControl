#include "HttpServerMgr.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include "WechatPayCrypto.h"

// 👑 极其重要：在包含 httplib 之前，必须定义这个宏，它才会启用 OpenSSL 支持！
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

// 假设这是你的 32 位 APIv3 密钥 (实际项目中建议后续写在 Config.ini 里动态读取)
const QString API_V3_KEY = "Your_32_Byte_API_V3_Key_Here_123";

HttpServerMgr::HttpServerMgr(QObject* parent) : QObject(parent), m_isRunning(false) {
    m_server = std::make_unique<httplib::Server>();
}

HttpServerMgr::~HttpServerMgr()
{
    qDebug() << "~HttpServerMgr";
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

        // 1. 获取微信发来的 Header (TODO: 后续用于 RSA 签名验证)
        auto sig = req.get_header_value("Wechatpay-Signature");
        auto timestamp = req.get_header_value("Wechatpay-Timestamp");
        auto headerNonce = req.get_header_value("Wechatpay-Nonce");
        auto serial = req.get_header_value("Wechatpay-Serial");

        // 2. 将收到的 Body 密文解析为 JSON
        QByteArray bodyData = QByteArray::fromStdString(req.body);
        QJsonParseError jsonErr;
        QJsonDocument doc = QJsonDocument::fromJson(bodyData, &jsonErr);

        if (jsonErr.error != QJsonParseError::NoError) {
            qDebug() << u8"[HttpServer] 回调包 JSON 格式错误:" << jsonErr.errorString();
            res.status = 400; // 格式错误，打回
            return;
        }

        QJsonObject rootObj = doc.object();
        QString eventType = rootObj["event_type"].toString();

        // 确保这是支付成功的通知 (微信还会发退款等其他通知，需要甄别)
        if (eventType != "TRANSACTION.SUCCESS") {
            qDebug() << u8"[HttpServer] 收到非支付成功通知，类型:" << eventType;
            res.status = 200; // 告诉微信收到了，不用再发了
            return;
        }

        // 3. 提取加密用的三个关键参数
        QJsonObject resourceObj = rootObj["resource"].toObject();
        QString associatedData = resourceObj["associated_data"].toString();
        QString resNonce = resourceObj["nonce"].toString();
        QString ciphertext = resourceObj["ciphertext"].toString();

        // =========================================================================
        // 👑 4. 核心解密：调用 OpenSSL 的 AEAD_AES_256_GCM 解开 Body
        // =========================================================================
        QString plainJsonStr = WechatPayCrypto::DecryptAES256GCM(API_V3_KEY, associatedData, resNonce, ciphertext);
        if (plainJsonStr.isEmpty()) {
            qDebug() << u8"[HttpServer] AES-GCM 密文解密失败！请检查 APIv3 密钥是否正确！";
            res.status = 500; // 返回 500 让微信进入重试队列
            return;
        }

        qDebug() << u8"[HttpServer] 解密成功，明文内容:" << plainJsonStr;

        // 5. 解析明文 JSON，提取终极订单号
        QJsonDocument plainDoc = QJsonDocument::fromJson(plainJsonStr.toUtf8());
        QJsonObject plainObj = plainDoc.object();

        QString out_trade_no = plainObj["out_trade_no"].toString();                                                         // 本地系统待支付订单号 (如: PAY2026...)
        QString transaction_id = plainObj["transaction_id"].toString();                                                     // 微信的真实流水号 (如: 420000...)
        QString trade_state = plainObj["trade_state"].toString();                                                           // 状态，正常必是 "SUCCESS"

        // 6. 验证状态并抛出跨线程信号给主业务引擎！
        if (trade_state == "SUCCESS" && !out_trade_no.isEmpty()) {
            emit SigWechatPaySuccess(out_trade_no, transaction_id);
        }

        // 7. 给微信返回标准 200 OK 和 JSON，否则微信的定时器会疯狂重发回调
        res.status = 200;
        res.set_content("{\"code\": \"SUCCESS\", \"message\": \"OK\"}", "application/json");
        });

    // ---------------------------------------------------------
    // 👑 启动阻塞监听 (此时跑在你的 ThreadPool 专属网络线程中)
    // ---------------------------------------------------------
    m_isRunning = true;
    qDebug() << u8"[HttpServer] 开始监听微信回调，端口:" << port; // 移到上面，防止被 listen 阻塞

    m_server->listen("0.0.0.0", port);

    qDebug() << u8"[HttpServer] 监听已退出";
}

void HttpServerMgr::Stop()
{
    if (m_isRunning)
    {
        m_server->stop();
        m_isRunning = false;
    }
}