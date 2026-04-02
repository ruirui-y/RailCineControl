#include "TcpServer.h"
#include "ThreadPool.h"

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
    m_sessions.clear();

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

    // 3. 保存到 Map 里维持生命周期
    m_sessions.insert(socketDescriptor, session);

    qDebug() << u8"[TcpServer] 客户端接入完毕，当前在线人数:" << GetOnlineCount();
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