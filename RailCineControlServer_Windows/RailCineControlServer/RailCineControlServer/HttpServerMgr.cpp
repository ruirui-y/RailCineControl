#include "HttpServerMgr.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

// 👑 我们把 OpenSSL 的宏也去掉了，彻底断绝 DLL 冲突的后患！
// 因为现在是由 cpolar 接收外网的 HTTPS，转换成普通 HTTP 塞给我们的 8182 端口
#include "httplib.h"

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
    // 👑 注册中台支付的专属 Webhook 路由 (接收明文 JSON)
    // ---------------------------------------------------------
    m_server->Post("/api/wechat/pay_notify", [this](const httplib::Request& req, httplib::Response& res) {
        qDebug() << u8"[HttpServer] 收到中台支付回调请求！";

        // 1. 直接将收到的 Body 解析为 JSON
        QByteArray bodyData = QByteArray::fromStdString(req.body);
        QJsonParseError jsonErr;
        QJsonDocument doc = QJsonDocument::fromJson(bodyData, &jsonErr);

        if (jsonErr.error != QJsonParseError::NoError) {
            qDebug() << u8"[HttpServer] 回调包 JSON 格式错误:" << jsonErr.errorString() << " 内容:" << req.body.c_str();
            res.status = 400; // 格式错误，打回
            return;
        }

        QJsonObject rootObj = doc.object();
        qDebug() << u8"[HttpServer] 中台回调明文内容:" << QString::fromUtf8(bodyData);

        // =========================================================================
        // 👑 2. 提取回调字段 
        // ⚠️ 极客提醒：这里的字段名我做了一些兼容猜测。
        // 最好的做法是让你的同事用 Apifox 或者直接扫码付一笔，
        // 看看 qDebug 打印出来的“中台回调明文内容”长什么样，然后把这里的键值对齐！
        // =========================================================================

        // 提取咱们系统的本地订单号 (同时兼容驼峰和下划线写法)
        QString out_trade_no = rootObj["outTradeNo"].toString();
        if (out_trade_no.isEmpty()) out_trade_no = rootObj["out_trade_no"].toString();

        // 提取真实的微信流水号 (非必填，主要是存库备查)
        QString transaction_id = rootObj["transactionId"].toString();
        if (transaction_id.isEmpty()) transaction_id = rootObj["transaction_id"].toString();

        // 3. 验证并抛出跨线程信号给主业务引擎！
        // 正常来说，中台既然回调了我们，就代表一定是支付成功了。
        if (!out_trade_no.isEmpty()) {
            qDebug() << u8"[HttpServer] 确认订单支付成功，派发 TCP 通知！订单号:" << out_trade_no;
            emit SigWechatPaySuccess(out_trade_no, transaction_id);
        }
        else {
            qDebug() << u8"[HttpServer] 回调中没有解析到订单号，忽略该回调。";
        }

        // 4. 给中台返回标准 200 OK，告诉它“我收到了，别再发了”
        // ⚠️ 也可以问问同事：他希望你返回 "SUCCESS" 还是 JSON 格式。
        res.status = 200;
        res.set_content("{\"code\": \"SUCCESS\", \"message\": \"OK\"}", "application/json");
        });

    // ---------------------------------------------------------
    // 👑 启动阻塞监听 (此时跑在你的 ThreadPool 专属网络线程中)
    // ---------------------------------------------------------
    m_isRunning = true;
    qDebug() << u8"[HttpServer] 开始监听中台回调，端口:" << port;

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