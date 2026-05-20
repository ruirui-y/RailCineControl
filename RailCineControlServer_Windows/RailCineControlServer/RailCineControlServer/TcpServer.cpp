#include "TcpServer.h"
#include "ThreadPool.h"

TcpServer::TcpServer(QObject* parent)
{
    // 当 HTTP 服务器发现微信回调成功时，触发这里的处理逻辑
    connect(ThreadPool::Instance()->GetHttpMgr(), &HttpServerMgr::SigWechatPaySuccess,
        this, &TcpServer::OnWechatPaySuccess);
}

TcpServer::~TcpServer()
{
    // 1. 停止接收新连接
    if (this->isListening()) {
        this->close();
    }

    // 2. 清空 Map。这会瞬间触发所有 ClientSession 智能指针的析构！
    // 配合我们之前写的 safeDeleter，它们会在各自的 Worker 线程中优雅死亡
    m_sessions.clear();

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

    // 3. 保存到 Map 里维持生命周期
    m_sessions.insert(socketDescriptor, session);

    qDebug() << u8"[TcpServer] 客户端接入完毕，当前在线人数:" << GetOnlineCount();
}

void TcpServer::OnWechatPaySuccess(const QString& out_trade_no, const QString& transaction_id)
{
    qDebug() << u8"[TcpServer] 收到微信回调，开始结算订单:" << out_trade_no;

    // 1. 第一步：先查询订单的基本信息 (userId 和 积分奖励)
    QString sqlInfo = "SELECT o.user_id, g.points_reward FROM t_pay_order o "
        "JOIN t_goods_sku g ON o.goods_id = g.goods_id "
        "WHERE o.order_id = ? AND o.status = 0";

    QList<QVariant> params;
    params << out_trade_no;

    ThreadPool::Instance()->PostQueryTask(sqlInfo, [this, out_trade_no, transaction_id](const QList<QVariantMap>& results) {
        if (results.isEmpty()) {
            qDebug() << u8"[TcpServer] 订单无效或已处理过:" << out_trade_no;
            return;
        }

        uint64_t userId = results.first()["user_id"].toULongLong();
        int pointsReward = results.first()["points_reward"].toInt();

        // =========================================================================
        // 2. 核心事务：将【更新订单】、【增加余额】、【写入流水】打包为一个原子操作
        // =========================================================================
        QList<QString> sqls;
        QVariantList allParams;

        // A. 更新订单状态为已支付
        sqls << "UPDATE t_pay_order SET status = 1, third_party_no = ?, pay_time = NOW() WHERE order_id = ?";
        allParams << transaction_id << out_trade_no;

        // B. 给用户钱包加钱
        sqls << "UPDATE t_user_wallet SET balance_points = balance_points + ?, total_recharged = total_recharged + ? WHERE user_id = ?";
        allParams << pointsReward << pointsReward << userId;

        // C. 写入流水记录 (balance_after 通过子查询实时获取，确保账目绝对精确)
        sqls << "INSERT INTO t_point_flow (user_id, flow_type, points_change, balance_after, associated_id, create_time) "
            "VALUES (?, 1, ?, (SELECT balance_points FROM t_user_wallet WHERE user_id = ?), ?, NOW())";
        allParams << userId << pointsReward << userId << out_trade_no;

        ThreadPool::Instance()->PostTransactionTask(sqls, [this, userId, out_trade_no](bool success) {
            if (!success) {
                qDebug() << u8"[TcpServer] 支付事务执行失败，订单号:" << out_trade_no;
                return;
            }

            // =========================================================================
            // 3. 异步主动推送：此时事务已提交，数据库已落地，开始通知客户端
            // =========================================================================
            // 💡 优化：直接使用 userId 索引。建议在 TcpServer 维护 QMap<uint64_t, std::shared_ptr<ClientSession>> m_userSessions
            auto it = m_sessions.find(userId);
            if (it != m_sessions.end())
            {
                auto session = it.value();

                // 再次查询该用户最新的余额，准备推给 UI
                QString sqlBalance = "SELECT balance_points FROM t_user_wallet WHERE user_id = ?";
                QList<QVariant> bParams;
                bParams << userId;

                ThreadPool::Instance()->PostQueryTask(sqlBalance, [this, session, userId, out_trade_no](const QList<QVariantMap>& bResults) {
                    if (bResults.isEmpty()) return;
                    qint64 currentPoints = bResults.first()["balance_points"].toLongLong();

                    // 构造并发送推送包
                    ServerApi::OrderNotifyPush push;
                    push.set_order_id(out_trade_no.toStdString());
                    push.set_is_success(true);
                    push.set_current_points(currentPoints);

                    session->SendProtoMsg(ServerApi::MsgId::ID_ORDER_NOTIFY_PUSH, push);
                    qDebug() << u8"[TcpServer] 实时推送充值成功信号，用户:" << userId << u8"余额:" << currentPoints;
                    }, true, bParams);
            }
            else {
                qDebug() << u8"[TcpServer] 订单结算完成，但用户已下线，无需推送。";
            }
            }, true, allParams);

        }, true, params);
}

int TcpServer::GetOnlineCount() const
{
    int validUserCount = 0;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value()->IsLogined()) 
        { // 假设你给 m_isLogined 写了个 Get 方法
            validUserCount++;
        }
    }
    return validUserCount;
}

void TcpServer::StartCleanupUnpaidTask()
{
    m_cleanupUnpaidTaskTimer = new QTimer(this);

    connect(m_cleanupUnpaidTaskTimer, &QTimer::timeout, this, [this]() {
        // 第一步：把 5 分钟未支付的订单逻辑关闭 (status = -1)
        QString sqlUpdate = "UPDATE t_pay_order SET status = -1 "
            "WHERE status = 0 AND create_time <= DATE_SUB(NOW(), INTERVAL 5 MINUTE);";

        ThreadPool::Instance()->PostUpdateTask(sqlUpdate, [](int affectedRows) {
            if (affectedRows > 0) {
                qDebug() << u8"[清理守护] 逻辑关闭了 " << affectedRows << u8" 个超时订单。";
            }
            }, true);

        // 第二步：把关闭超过 3 天的陈年死单彻底从硬盘物理删除，释放空间
        QString sqlDelete = "DELETE FROM t_pay_order "
            "WHERE status = -1 AND create_time <= DATE_SUB(NOW(), INTERVAL 3 DAY);";

        ThreadPool::Instance()->PostUpdateTask(sqlDelete, [](int deletedRows) {
            if (deletedRows > 0) {
                qDebug() << u8"[清理守护] 物理销毁了 " << deletedRows << u8" 个历史僵尸订单。";
            }
            }, true);
        });

    m_cleanupUnpaidTaskTimer->start(60 * 1000); // 依然是每分钟巡检一次
}

void TcpServer::onSessionClosed(ClientSession* session)
{
    // 找出是谁断开了
    qintptr handle = 0;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().get() == session) {
            handle = it.key();
            break;
        }
    }

    if (handle != 0)
    {
        // 从 Map 中移除，智能指针引用计数归零，ClientSession 自动触发析构销毁！
        m_sessions.remove(handle);
        qDebug() << u8"[TcpServer] 客户端已清理，当前在线人数:" << GetOnlineCount();
    }
}