#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <memory>
#include <functional>
#include "common.pb.h"
#include "server_msg.pb.h"


class ClientSession : public QObject, public std::enable_shared_from_this<ClientSession>
{
    Q_OBJECT

public:
    explicit ClientSession(qintptr socketDescriptor, QObject* parent = nullptr);
    ~ClientSession();

    // 发送 Proto 业务包到客户端
    void SendProtoMsg(ServerApi::MsgId msgId, const google::protobuf::Message& protoMsg,
        uint64_t seqId = 0,
        ServerApi::ErrorCode errCode = ServerApi::ERR_SUCCESS,
        const QString& errMsg = "");

public:
    // =====================================================================
    // 👑 状态访问器 (Inline Getters & Setters) - 绝对零开销
    // =====================================================================
    qintptr GetSocketDescriptor() const { return m_socketDescriptor; }          // 获取底层网络句柄

    bool IsLogined() const { return m_isLogined; }                              // 获取登录状态
    void SetLogined(bool logined) { m_isLogined = logined; }                    // 设置登录状态

    int GetAccountId() const { return m_accountId; }                            // 获取用户账号 ID
    void SetAccountId(int accountId) { m_accountId = accountId; }               // 设置用户账号 ID

    QString GetUsername() const { return m_username; }                          // 获取用户账号名称
    void SetUsername(const QString& username) { m_username = username; }        // 设置用户账号名称

    qint64 GetLastDbUpdateTime() const { return m_lastDbUpdateTime; }           // 获取上次写库时间
    void SetLastDbUpdateTime(qint64 time) { m_lastDbUpdateTime = time; }        // 设置上次写库时间

    qint64 GetLastDbSyncTime() const { return m_lastDbSyncTime; }               // 获取上次心跳同步时间
    void SetLastDbSyncTime(qint64 time) { m_lastDbSyncTime = time; }            // 设置上次心跳同步时间

    qint64 GetLastRecvTime() const { return m_lastRecvTime; }                   // 获取上次收包时间
    void SetLastRecvTime(qint64 time) { m_lastRecvTime = time; }                // 设置上次收包时间

    // =====================================================================
    // 👑 信号触发器 (防止外部直接 emit 破坏封装)
    // =====================================================================
    void TriggerLoginSuccess() {                                                // 触发登录成功信号
        emit SigSessionLoginSuccess(m_socketDescriptor, m_accountId);
    }

    void TriggerRequestDisconnect() {                                           // 触发请求断开信号
        emit SigRequestDisconnect();
    }

signals:
    void SigRequestDisconnect();                                                // 定义请求断开socket
    void SigSessionLoginSuccess(qintptr fd, uint64_t userId);                   // 告诉主服务器用户登录成功
    void SigSessionClosed(qintptr fd);                                          // 告诉主服务器：我断开了，请把我从 Map 中移除并销毁

private slots:
    void onReadyRead();                                                         // 核心：粘包/半包拆解逻辑
    void onDoDisconnect();                                                      // 主动断开连接
    void onDisconnected();                                                      // 核心：处理网络异常或正常断开
    void CheckTimeout();                                                        // 定时检查超时的槽函数

private:
    qintptr m_socketDescriptor;                                                 // 操作系统底层的网络句柄
    QTcpSocket* m_tcpSocket = nullptr;                                          // 真正的通信实体 (将在子线程创建)
    QByteArray m_buffer;                                                        // 数据缓冲区，用于解决 TCP 粘包

    // --- 会话专属的业务数据 ---
    int m_accountId = -1;                                                       // 记录该会话绑定的客户账号 ID
    bool m_isLogined = false;                                                   // 标记该客户端是否已经通过账号验证
    QString m_username = "";                                                    // 记录该会话绑定的客户账号名称

    qint64 m_lastDbUpdateTime = 0;                                              // 记录上次向数据库写入心跳的时间

    QTimer* m_heartbeatTimer = nullptr;                                         // 心跳定时器
    qint64 m_lastRecvTime = 0;                                                  // 上次收到任何数据的时间戳
    qint64 m_lastDbSyncTime = 0;                                                // 记录上一次真实写入数据库的心跳时间 (秒)
};