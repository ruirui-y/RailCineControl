#include "AuthService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include <QDateTime>
#include <QDebug>

AuthService::~AuthService()
{
    qDebug() << u8"♻️ [AuthService] 实例已安全析构释放";
}

void AuthService::Init()
{
    // 👑 绝杀防御：获取自身的弱引用，防止 MsgDispatcher 调到已经析构的 AuthService
    std::weak_ptr<AuthService> weakSelf = shared_from_this();

    // 注册登录方法
    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_LOGIN_REQ,
        [weakSelf](std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& body) 
        {
            //qDebug() << u8"[AuthService] 路由回调触发，当前执行线程ID:" << QThread::currentThreadId()
            //    << u8"当前线程名字:" << QThread::currentThread()->objectName();
            if (auto strongSelf = weakSelf.lock()) {
                strongSelf->OnLogin(session, header, body);
            }
        });

    // 注册心跳方法
    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_HEARTBEAT,
        [weakSelf](std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& body)
        {
            //qDebug() << u8"[AuthService] 路由回调触发，当前执行线程ID:" << QThread::currentThreadId()
            //    << u8"当前线程名字:" << QThread::currentThread()->objectName();
            if (auto strongSelf = weakSelf.lock()) {
                strongSelf->OnHeartbeat(session, header, body);
            }
        });
}

void AuthService::OnLogin(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    uint64_t seq_id = header.seq_id();
    ServerApi::LoginReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString loginUser = QString::fromStdString(req.username());
    QString loginPwd = QString::fromStdString(req.password());

    qDebug() << u8"[AuthService] 收到登录请求，账号:" << loginUser;

    // 🌟 保护网络连接：防止查库时客户端断线
    std::weak_ptr<ClientSession> weakSession = session;

    QString sql = "SELECT id, password, shop_name, expire_time, status, last_heartbeat_time, user_permissions FROM sys_account WHERE username = ?";
    QList<QVariant> params;
    params << loginUser;

    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, loginUser, loginPwd, seq_id](const QList<QVariantMap>& results) {
        auto strongSession = weakSession.lock();
        if (!strongSession) return;

        ServerApi::LoginRsp emptyRsp;

        if (results.isEmpty()) {
            strongSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"账号不存在");
            return;
        }

        const QVariantMap& row = results.first();

        // 👑 改造：通过 Setter 注入数据
        strongSession->SetAccountId(row["id"].toInt());

        QString dbPwd = row["password"].toString();
        QString shopName = row["shop_name"].toString();
        QDateTime expireTime = row["expire_time"].toDateTime();
        int status = row["status"].toInt();
        int userPermissions = row["user_permissions"].toInt();

        // 密码、状态等校验
        if (loginPwd != dbPwd) {
            strongSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"密码错误");
            return;
        }
        if (status == 0) {
            strongSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"该账号已被停用，请联系厂家");
            return;
        }
        if (QDateTime::currentDateTime() > expireTime) {
            strongSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_ACCOUNT_EXPIRED, u8"账号授权已过期，请联系续费");
            return;
        }

        // ---------------------------------------------------------
        // 成功逻辑：通过 Setter 状态写回 Session
        // ---------------------------------------------------------
        strongSession->SetLogined(true);
        strongSession->SetUsername(loginUser);

        // 👑 改造：调用专用的触发器发射登录成功信号
        strongSession->TriggerLoginSuccess();

        ServerApi::LoginRsp successRsp;
        successRsp.set_server_time(QDateTime::currentMSecsSinceEpoch());
        successRsp.set_shop_name(shopName.toStdString());
        successRsp.set_permission(userPermissions);

        strongSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, successRsp, seq_id, ServerApi::ERR_SUCCESS, "");
        qDebug() << u8"[AuthService] 账号登录成功:" << loginUser << u8"门店:" << shopName;

        QString updateSql = "UPDATE sys_account SET last_heartbeat_time = NOW(), last_login_time = NOW() WHERE id = ?";
        QList<QVariant> updateParams;
        // 👑 改造：通过 Getter 取出数据
        updateParams << strongSession->GetAccountId();

        ThreadPool::Instance()->PostUpdateTask(updateSql, [](bool) {}, true, updateParams);

        }, true, params);
}

void AuthService::OnHeartbeat(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::Heartbeat hbRsp;
    hbRsp.set_timestamp(QDateTime::currentMSecsSinceEpoch());
    session->SendProtoMsg(ServerApi::ID_HEARTBEAT, hbRsp);

    // 👑 改造：统一使用 Getter 访问状态
    if (!session->IsLogined() || session->GetUsername().isEmpty()) return;

    qint64 currentMsecs = QDateTime::currentMSecsSinceEpoch();
    if (currentMsecs - session->GetLastDbUpdateTime() > 60000) {

        // 👑 改造：更新时间戳
        session->SetLastDbUpdateTime(currentMsecs);

        QString updateSql = "UPDATE sys_account SET last_login_time = NOW() WHERE username = ?";
        QList<QVariant> params;
        params << session->GetUsername();

        ThreadPool::Instance()->PostUpdateTask(updateSql, [](bool) {}, true, params);
    }
}