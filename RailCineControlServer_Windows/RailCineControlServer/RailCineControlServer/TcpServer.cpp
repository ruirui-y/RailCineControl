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
    // 1. 先找到当前登录成功的这个新会话
    auto it = m_fdSessions.find(fd);
    if (it == m_fdSessions.end()) return;
    auto newSession = it.value();

    // 2. 检查 UserId 是否已在业务表中存在 (互斥登录检测)
    if (m_userSessions.contains(userId)) {
        auto oldSession = m_userSessions.value(userId);

        qDebug() << u8"🔄 [TcpServer] 检测到账号重复登录，正在踢出旧连接，UserID:" << userId;

        // A. 给旧连接发送“被顶号”的提示 (利用原有的 SendProtoMsg)
        // 注意：这里需要一个 seq_id，如果旧连接没有 seq_id，传 0 即可
        ServerApi::LoginRsp kickRsp;
        oldSession->SendProtoMsg(ServerApi::ID_LOGIN_RSP, kickRsp, 0,
            ServerApi::ERR_ACCOUNT_IN_USE, u8"账号已在其他终端活跃");

        // B. 强制断开旧连接 (让 TCP 链路断开，触发 onDisconnected 自动清理)
        // 这一步之后，TcpServer 的 onSessionClosed 会自动被触发，
        // 在 onSessionClosed 中我们会处理从 m_fdSessions 和 m_userSessions 的移除逻辑
        oldSession->TriggerRequestDisconnect();
    }

    // 3. 注册新会话到业务表
    m_userSessions.insert(userId, newSession);

    qDebug() << u8"[TcpServer] 用户登录注册完成，UserID:" << userId
        << u8"当前在线人数:" << m_userSessions.size();
}

void TcpServer::onSessionClosed(qintptr fd)
{
    // 1. 获取即将断开的会话
    auto it = m_fdSessions.find(fd);
    if (it == m_fdSessions.end()) return;

    auto session = it.value();
    uint64_t userId = session->GetAccountId();

    // 2. 关键判断：仅当业务表中存在的会话确实是当前这个 fd 时，才执行移除
    // 如果 m_userSessions[userId] 不等于当前的 session，说明已经被“顶”了，
    // 此时它指向的是新用户的会话，我们绝对不能动它！
    if (userId != 0) {
        auto userIt = m_userSessions.find(userId);
        if (userIt != m_userSessions.end() && userIt.value() == session)
        {
            m_userSessions.erase(userIt);
            qDebug() << u8"[TcpServer] 用户主动断开，从业务表移除 UserID:" << userId
                << u8"，当前剩余在线用户数:" << m_userSessions.size();
        }
        else
        {
            qDebug() << u8"[TcpServer] 收到下线通知，但该会话已被顶替，跳过业务表移除，UserID:" << userId;
        }
    }

    // 3. 从物理 Map 彻底销毁连接
    m_fdSessions.remove(fd);
    qDebug() << u8"[TcpServer] 连接已清理，当前剩余连接数:" << m_fdSessions.size();
}

void TcpServer::SendPushToUser(uint64_t userId, ServerApi::MsgId msgId, const google::protobuf::Message& msg)
{
    auto it = m_userSessions.find(userId);
    if (it != m_userSessions.end())
    {
        it.value()->SendProtoMsg(msgId, msg);
    }
}