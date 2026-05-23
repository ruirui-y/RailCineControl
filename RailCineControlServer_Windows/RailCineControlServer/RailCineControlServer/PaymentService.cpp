#include "PaymentService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include "Global.h"           
#include "MidPlatformManager.h"
#include "Enum.h"
#include "TcpServer.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QDateTime>

PaymentService::~PaymentService() { qDebug() << u8"♻️ [PaymentService] 析构"; }

void PaymentService::Init()
{
    std::weak_ptr<PaymentService> weakSelf = shared_from_this();

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_CREATE_ORDER_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnCreateOrder(s, h, b); });
}

void PaymentService::OnCreateOrder(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::CreateOrderReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    uint64_t seq_id = header.seq_id();
    uint64_t goods_id = req.goods_id();

    std::weak_ptr<ClientSession> weakSession = session;
    std::weak_ptr<PaymentService> weakSelf = shared_from_this();

    // Step 1: 校验商品
    QString sqlGoods = "SELECT price_cents FROM t_goods_sku WHERE goods_id = ? AND status = 1";
    ThreadPool::Instance()->PostQueryTask(sqlGoods, [weakSession, weakSelf, seq_id, goods_id](const QList<QVariantMap>& results) {
        auto s = weakSession.lock();
        auto self = weakSelf.lock();
        if (!s || !self) return;

        if (results.isEmpty()) {
            s->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id, ServerApi::ERR_GOODS_OFFLINE, u8"商品不存在");
            return;
        }

        int priceCents = results.first()["price_cents"].toInt();
        QString orderId = QString("PAY_%1_%2").arg(QDateTime::currentDateTime().toString("yyyy_MMdd_HH_mm_ss")).arg(s->GetAccountId());

        // Step 2: 创建本地订单
        QString sqlOrder = "INSERT INTO t_pay_order (order_id, user_id, goods_id, amount_cents, status, create_time) VALUES (?, ?, ?, ?, 0, NOW())";
        ThreadPool::Instance()->PostUpdateTask(sqlOrder, [weakSession, seq_id, orderId, priceCents](bool success) {
            auto s = weakSession.lock();
            if (!s || !success) {
                if (s) s->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id, ServerApi::ERR_CREATE_ORDER_FAILED, u8"创建订单失败");
                return;
            }

            // Step 3: 请求中台 (注意：此处发起同步HTTP，若并发极高建议也做成异步处理)
            QString token = MidPlatformManager::Instance()->GetAccessToken();
            if (token.isEmpty()) {
                s->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id, ServerApi::ERR_GENERATE_TOKEN_FAILED, u8"Token获取失败");
                return;
            }

            QJsonObject reqObj;
            reqObj["outTradeNo"] = orderId;
            reqObj["amount"] = 0.01; // 测试金额
            reqObj["subject"] = u8"充值积分";
            reqObj["paymentMethod"] = 2;
            reqObj["merchantId"] = "1725620235";
            reqObj["appId"] = "wxc8d0411c217a8b4c";
            reqObj["callbackUrl"] = GlobalConfig::Instance()->GetWxNotifyUrl();
            reqObj["expirationTime"] = QDateTime::currentDateTime().addSecs(300).toString(Qt::ISODate) + "+08:00";

            QNetworkAccessManager manager;
            QNetworkRequest netReq(QUrl("https://api.stg.playlink.games/open/transactions"));
            netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            netReq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

            QNetworkReply* reply = manager.post(netReq, QJsonDocument(reqObj).toJson());
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject resObj = QJsonDocument::fromJson(reply->readAll()).object();
                QString qrCodeUrl = resObj["paymentParams"].toObject().value("QrcodeUrl").toString();

                ServerApi::CreateOrderRsp rsp;
                rsp.set_order_id(orderId.toStdString());
                rsp.set_qr_code_url(qrCodeUrl.toStdString());
                rsp.set_expire_time(300);
                s->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, rsp, seq_id);
            }
            else {
                s->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id, ServerApi::ERR_PAY_API_FAILED, u8"下单接口异常");
            }
            reply->deleteLater();
            }, true, QList<QVariant>() << orderId << s->GetAccountId() << goods_id << priceCents);
        }, true, QList<QVariant>() << goods_id);
}

void PaymentService::ProcessPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status)
{
    qDebug() << u8"[PaymentService] 开始处理订单状态变更:" << out_trade_no << u8"状态码:" << payment_status;

    // =========================================================================
    // ❌ 失败/关闭分支
    // =========================================================================
    if (payment_status == PAY_STATUS_CLOSED || payment_status == PAY_STATUS_REFUNDED) {
        QString sqlInfo = "SELECT user_id FROM t_pay_order WHERE order_id = ? AND status = 0";
        ThreadPool::Instance()->PostQueryTask(sqlInfo, [this, out_trade_no](const QList<QVariantMap>& results) {
            if (results.isEmpty()) return;

            uint64_t userId = results.first()["user_id"].toULongLong();
            QString sqlClose = "UPDATE t_pay_order SET status = -1 WHERE order_id = ? AND status = 0";

            ThreadPool::Instance()->PostUpdateTask(sqlClose, [this, out_trade_no, userId](int affectedRows) {
                if (affectedRows > 0) {
                    qDebug() << u8"🗑️ [PaymentService] 订单已作废:" << out_trade_no;

                    ServerApi::OrderNotifyPush push;
                    push.set_order_id(out_trade_no.toStdString());
                    push.set_is_success(false);
                    push.set_current_points(0);

                    // 👑 通过 TcpServer 引用进行推送
                    m_server.SendPushToUser(userId, ServerApi::ID_ORDER_NOTIFY_PUSH, push);
                }
                }, true, QList<QVariant>() << out_trade_no);
            }, true, QList<QVariant>() << out_trade_no);
        return;
    }

    // =========================================================================
    // ✅ 成功分支
    // =========================================================================
    if (payment_status == PAY_STATUS_SUCCESS) {
        QString sqlInfo = "SELECT o.user_id, g.points_reward FROM t_pay_order o "
            "JOIN t_goods_sku g ON o.goods_id = g.goods_id "
            "WHERE o.order_id = ? AND o.status = 0";

        ThreadPool::Instance()->PostQueryTask(sqlInfo, [this, out_trade_no, transaction_id](const QList<QVariantMap>& results) {
            if (results.isEmpty()) return;

            uint64_t userId = results.first()["user_id"].toULongLong();
            int pointsReward = results.first()["points_reward"].toInt();

            // 👑 事务执行
            QList<QString> sqls;
            QVariantList allParams;

            sqls << "UPDATE t_pay_order SET status = ?, third_party_no = ?, pay_time = NOW() WHERE order_id = ?";
            allParams << QVariant(QVariantList() << ORDER_STATUS_PAID << transaction_id << out_trade_no);

            sqls << "UPDATE t_user_wallet SET balance_points = balance_points + ?, total_recharged = total_recharged + ? WHERE user_id = ?";
            allParams << QVariant(QVariantList() << pointsReward << pointsReward << userId);

            sqls << "INSERT INTO t_point_flow (user_id, flow_type, points_change, balance_after, associated_id, create_time) "
                "VALUES (?, 1, ?, (SELECT balance_points FROM t_user_wallet WHERE user_id = ?), ?, NOW())";
            allParams << QVariant(QVariantList() << userId << pointsReward << userId << out_trade_no);

            ThreadPool::Instance()->PostTransactionTask(sqls, [this, userId, out_trade_no](bool success) {
                if (!success) return;

                // 查询最新余额并推送
                QString sqlBalance = "SELECT balance_points FROM t_user_wallet WHERE user_id = ?";
                ThreadPool::Instance()->PostQueryTask(sqlBalance, [this, userId, out_trade_no](const QList<QVariantMap>& bResults) {
                    if (bResults.isEmpty()) return;
                    qint64 currentPoints = bResults.first()["balance_points"].toLongLong();

                    ServerApi::OrderNotifyPush push;
                    push.set_order_id(out_trade_no.toStdString());
                    push.set_is_success(true);
                    push.set_current_points(currentPoints);

                    m_server.SendPushToUser(userId, ServerApi::ID_ORDER_NOTIFY_PUSH, push);
                    qDebug() << u8"🎉 [PaymentService] 推送充值成功，用户:" << userId;
                    }, true, QList<QVariant>() << userId);
                }, true, allParams);
            }, true, QList<QVariant>() << out_trade_no);
    }
}