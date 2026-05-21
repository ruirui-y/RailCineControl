#include "TcpServer.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QUrl>
#include <QDebug>

#include "MidPlatformManager.h"
#include "ThreadPool.h"
#include "Enum.h"


TcpServer::TcpServer(QObject* parent)
{
    // 当 HTTP 服务器发现微信回调成功时，触发这里的处理逻辑
    connect(ThreadPool::Instance()->GetHttpMgr(), &HttpServerMgr::SigPaymentResult,
        this, &TcpServer::OnPaymentResult);

    connect(MidPlatformManager::Instance().get(), &MidPlatformManager::SigPaymentResult,
        this, &TcpServer::OnPaymentResult);
}

TcpServer::~TcpServer()
{
    // 1. 停止接收新连接
    if (this->isListening()) {
        this->close();
    }

    // 2. 清空 Map。这会瞬间触发所有 ClientSession 智能指针的析构！
    // 配合我们之前写的 safeDeleter，它们会在各自的 Worker 线程中优雅死亡
    m_fdSessions.clear();

    // 3. 关闭清理未支付任务定时器
    if (m_cleanupUnpaidTaskTimer)
    {
        m_cleanupUnpaidTaskTimer->stop();
    }

    qDebug() << u8"[TcpServer] 服务器已关闭，所有资源已释放";
}

bool TcpServer::StartServer(quint16 port)
{
    // 如果已经在监听了，先关掉防止冲突
    if (this->isListening()) {
        this->close();
    }

    // 监听所有网卡 (Any) 的指定端口
    bool bSuccess = this->listen(QHostAddress::Any, port);

    if (bSuccess) {
        qDebug() << u8"[TcpServer] 启动成功！正在监听端口:" << port;
    }
    else {
        qDebug() << u8"[TcpServer] 启动失败，端口可能被占用:" << this->errorString();
    }

    // 定时清理未支付订单
    StartCleanupUnpaidTask();

    return bSuccess;
}

void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    qDebug() << u8"[TcpServer] 新客户端接入，句柄:" << socketDescriptor;

    // 1. 在 Worker 线程中创建智能指针管理的 Session (完美复用你的架构)
    auto session = ThreadPool::Instance()->GetThread()->CreateQObject<ClientSession, std::shared_ptr>(socketDescriptor);

    // 2. 绑定清理信号
    // 因为 TcpServer 在主线程，Session 在子线程，Qt 会自动使用 QueuedConnection 安全跨线程投递
    connect(session.get(), &ClientSession::SigSessionClosed, this, &TcpServer::onSessionClosed);
    connect(session.get(), &ClientSession::SigSessionLoginSuccess, this, &TcpServer::OnUserLoginSuccess);

    // 3. 保存到 Map 里维持生命周期
    m_fdSessions.insert(socketDescriptor, session);
}

void TcpServer::OnUserLoginSuccess(qintptr fd, uint64_t userId)
{
    auto it = m_fdSessions.find(fd);
    if (it != m_fdSessions.end()) {
        auto session = it.value();

        // 🌟 2. 核心操作：保存到业务 Map！
        // 如果这里发现 m_userSessions 里面已经有这个 userId 了，说明异地登录，可以触发顶号踢人逻辑！
        m_userSessions.insert(userId, session);

        qDebug() << u8"[TcpServer] 用户登录成功注册进业务表，UserID:" << userId << u8"[TcpServer] 客户端接入完毕，当前在线人数:" << m_userSessions.size();
    }
}

void TcpServer::OnPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status)
{
    qDebug() << u8"[TcpServer] 开始处理订单状态变更:" << out_trade_no << u8"状态码:" << payment_status;

    // =========================================================================
    // ❌ 失败/关闭分支 (PAY_STATUS_CLOSED = 关闭, PAY_STATUS_REFUNDED = 退款)
    // =========================================================================
    if (payment_status == PAY_STATUS_CLOSED || payment_status == PAY_STATUS_REFUNDED) {

        // 1. 先查出 user_id，以便给客户端发推送
        QString sqlInfo = "SELECT user_id FROM t_pay_order WHERE order_id = ? AND status = 0";
        QList<QVariant> params;
        params << out_trade_no;

        ThreadPool::Instance()->PostQueryTask(sqlInfo, [this, out_trade_no](const QList<QVariantMap>& results) {
            if (results.isEmpty()) {
                // 已经被处理过了，直接跳过
                return;
            }

            uint64_t userId = results.first()["user_id"].toULongLong();

            // 2. 将本地订单状态打为 -1
            QString sqlClose = "UPDATE t_pay_order SET status = -1 WHERE order_id = ? AND status = 0";
            QList<QVariant> paramsClose;
            paramsClose << out_trade_no;

            ThreadPool::Instance()->PostUpdateTask(sqlClose, [this, out_trade_no, userId](int affectedRows) {
                if (affectedRows > 0) {
                    qDebug() << u8"🗑️ [TcpServer] 收到中台关闭/退款回调，已将订单逻辑作废:" << out_trade_no;

                    // 3. 👑 补上主动推送！通知客户端关闭二维码弹窗
                    auto it = m_userSessions.find(userId);
                    if (it != m_userSessions.end()) {
                        ServerApi::OrderNotifyPush push;
                        push.set_order_id(out_trade_no.toStdString());
                        push.set_is_success(false);                                                                     // 明确告知客户端：支付失败/已取消
                        push.set_current_points(0);                                                                     // 失败情况下的积分无意义，传 0 即可

                        it.value()->SendProtoMsg(ServerApi::MsgId::ID_ORDER_NOTIFY_PUSH, push);
                        qDebug() << u8"[TcpServer] 已向客户端推送订单关闭通知，用户:" << userId;
                    }
                }
                }, true, paramsClose);

            }, true, params);

        return; // 处理完毕，退出当前分支
    }

    // =========================================================================
    // ✅ 成功分支 (PAY_STATUS_SUCCESS = 支付成功)
    // =========================================================================
    if (payment_status == PAY_STATUS_SUCCESS) {
        // 1. 查询订单的基本信息 (userId 和 积分奖励)
        QString sqlInfo = "SELECT o.user_id, g.points_reward FROM t_pay_order o "
            "JOIN t_goods_sku g ON o.goods_id = g.goods_id "
            "WHERE o.order_id = ? AND o.status = 0"; // status=0 防重放攻击的核心！

        QList<QVariant> params;
        params << out_trade_no;

        ThreadPool::Instance()->PostQueryTask(sqlInfo, [this, out_trade_no, transaction_id](const QList<QVariantMap>& results)
            {
                if (results.isEmpty()) {
                    qDebug() << u8"[TcpServer] 订单无效或已被处理，忽略本次成功回调:" << out_trade_no;
                    return;
                }

                uint64_t userId = results.first()["user_id"].toULongLong();
                int pointsReward = results.first()["points_reward"].toInt();

                // 2. 👑 核心事务：【更新订单】+【增加余额】+【写入流水】
                QList<QString> sqls;
                QVariantList allParams; // 这是一个“大框”，用来装每条SQL的“小框”

                // A. 更新订单状态为 1 (已支付)
                sqls << "UPDATE t_pay_order SET status = ?, third_party_no = ?, pay_time = NOW() WHERE order_id = ?";
                QVariantList params1; // 第1条SQL的专属小框
                params1 << ORDER_STATUS_PAID << transaction_id << out_trade_no;
                allParams << QVariant(params1); // 把小框塞进大框

                // B. 给用户钱包加钱
                sqls << "UPDATE t_user_wallet SET balance_points = balance_points + ?, total_recharged = total_recharged + ? WHERE user_id = ?";
                QVariantList params2; // 第2条SQL的专属小框
                params2 << pointsReward << pointsReward << userId;
                allParams << QVariant(params2);

                // C. 写入流水记录 (子查询实时获取最终余额)
                sqls << "INSERT INTO t_point_flow (user_id, flow_type, points_change, balance_after, associated_id, create_time) "
                    "VALUES (?, 1, ?, (SELECT balance_points FROM t_user_wallet WHERE user_id = ?), ?, NOW())";
                QVariantList params3; // 第3条SQL的专属小框
                params3 << userId << pointsReward << userId << out_trade_no;
                allParams << QVariant(params3);

                // 完美的 2D 参数列表传递
                ThreadPool::Instance()->PostTransactionTask(sqls, [this, userId, out_trade_no](bool success)
                    {
                        if (!success) {
                            qDebug() << u8"❌ [TcpServer] 致命错误：支付事务执行失败，订单号:" << out_trade_no;
                            return;
                        }

                        // 3. 异步主动推送：通知在线客户端刷新 UI
                        auto it = m_userSessions.find(userId);
                        if (it != m_userSessions.end()) {
                            auto session = it.value();

                            // 查询该用户最新余额
                            QString sqlBalance = "SELECT balance_points FROM t_user_wallet WHERE user_id = ?";
                            QList<QVariant> bParams;
                            bParams << userId;

                            ThreadPool::Instance()->PostQueryTask(sqlBalance, [session, userId, out_trade_no](const QList<QVariantMap>& bResults) {
                                if (bResults.isEmpty()) return;
                                qint64 currentPoints = bResults.first()["balance_points"].toLongLong();

                                // 下发 TCP 成功指令包
                                ServerApi::OrderNotifyPush push;
                                push.set_order_id(out_trade_no.toStdString());
                                push.set_is_success(true);
                                push.set_current_points(currentPoints);

                                session->SendProtoMsg(ServerApi::MsgId::ID_ORDER_NOTIFY_PUSH, push);
                                qDebug() << u8"🎉 [TcpServer] 实时推送充值成功信号，用户:" << userId << u8"余额:" << currentPoints;
                                }, true, bParams);
                        }
                        else {
                            qDebug() << u8"🎉 [TcpServer] 订单结算完成，但用户已下线，充值已入库。";
                        }
                }, true, allParams); // 结束事务
            },true, params);
    }
}

void TcpServer::StartCleanupUnpaidTask()
{
    m_cleanupUnpaidTaskTimer = new QTimer(this);

    connect(m_cleanupUnpaidTaskTimer, &QTimer::timeout, this, [this]() {
        // =========================================================================
        // 👑 第一阶段：查询所有面临过期的未支付订单 (ORDER_STATUS_PENDING)
        // =========================================================================
        QString sqlSelect = QString("SELECT order_id FROM t_pay_order WHERE status = %1 AND create_time <= DATE_SUB(NOW(), INTERVAL 5 MINUTE)")
            .arg(ORDER_STATUS_PENDING);

        ThreadPool::Instance()->PostQueryTask(sqlSelect, [this](const QList<QVariantMap>& results) {

            // 👑 新增：执行批量查询前的宏观日志打印
            if (!results.isEmpty()) {
                qDebug() << u8"🔍 [清理守护] 开始巡检，发现" << results.size() << u8"个超时未支付订单，准备向中台发起终极对账...";
            }

            for (const QVariantMap& row : results) {
                QString orderId = row["order_id"].toString();
                qDebug() << u8"   👉 正在查单:" << orderId;

                // 发起中台查单
                int realStatus = MidPlatformManager::Instance()->CheckOrderFromMidPlatform(orderId);

                // =========================================================================
                // 🛡️ 状态 1：明确未支付，安心送走 (ORDER_STATUS_CLOSED)
                // =========================================================================
                if (realStatus == PAY_STATUS_UNPAID) {
                    QString sqlClose = QString("UPDATE t_pay_order SET status = %1 WHERE order_id = ? AND status = %2")
                        .arg(ORDER_STATUS_CLOSED)
                        .arg(ORDER_STATUS_PENDING);
                    QList<QVariant> paramsClose;
                    paramsClose << orderId;

                    ThreadPool::Instance()->PostUpdateTask(sqlClose, [orderId](int affectedRows) {
                        if (affectedRows > 0) {
                            qDebug() << u8"   🗑️ [清理守护] 中台确认未支付，已本地作废:" << orderId;
                        }
                        }, true, paramsClose);
                }
                // =========================================================================
                // ⚠️ 状态 2：获取token失败
                // =========================================================================
                else if (realStatus == PAY_STATUS_ERROR) {
                    // 👑 绝不杀错！改为挂起态，等待次日对账或人工介入
                    QString sqlAnomaly = QString("UPDATE t_pay_order SET status = %1 WHERE order_id = ? AND status = %2")
                        .arg(ORDER_STATUS_ANOMALY)
                        .arg(ORDER_STATUS_PENDING);
                    QList<QVariant> paramsAnomaly;
                    paramsAnomaly << orderId;

                    ThreadPool::Instance()->PostUpdateTask(sqlAnomaly, [orderId](int affectedRows) {
                        if (affectedRows > 0) {
                            qDebug() << u8"   🚨 [清理守护-警报] 查单异常！已将订单置为挂起态，需后续核实:" << orderId;
                        }
                        }, true, paramsAnomaly);
                }
                // 注意：如果返回 PAY_STATUS_SUCCESS 或 PAY_STATUS_CLOSED，
                // 内部信号已经去触发 OnPaymentResult，这里无需操作！
            }
            }, true);

        // =========================================================================
        // 🗑️ 第二阶段：把陈年死单 (ORDER_STATUS_CLOSED) 彻底从硬盘物理删除
        // ⚠️ 注意：不要删除 ORDER_STATUS_ANOMALY 的异常订单，留给人工核查！
        // =========================================================================
        QString sqlDelete = QString("DELETE FROM t_pay_order WHERE status = %1 AND create_time <= DATE_SUB(NOW(), INTERVAL 3 DAY)")
            .arg(ORDER_STATUS_CLOSED);

        ThreadPool::Instance()->PostUpdateTask(sqlDelete, [](int) {}, true);
        });

    m_cleanupUnpaidTaskTimer->start(10 * 1000); // 10秒巡检一次
}

void TcpServer::onSessionClosed(qintptr fd)
{
    auto it = m_fdSessions.find(fd);
    if (it != m_fdSessions.end()) {
        auto session = it.value();
        uint64_t userId = session->GetUserId();                                         // 获取该会话绑定的 userId

        // 🌟 1. 如果他结过绑(登录过)，从业务 Map 移除
        if (userId != 0) {
            m_userSessions.remove(userId);
            qDebug() << u8"[TcpServer] 用户已下线，从业务表移除 UserID:" << userId << ", 当前剩余在线用户数:" << m_userSessions.size();
        }

        // 🌟 2. 从物理 Map 彻底销毁
        m_fdSessions.remove(fd);
        qDebug() << u8"[TcpServer] 连接已清理，当前剩余连接数:" << m_fdSessions.size();
    }
}