#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <memory>
#include <functional>
#include "common.pb.h"
#include "server_msg.pb.h"

// =========================================================================================
// 定义服务端路由回调类型
// =========================================================================================
using MsgHandler = std::function<void(const ServerApi::PacketHeader& header, const QByteArray& bodyData)>;

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
    bool IsLogined() const { return m_isLogined; }                              // 是否登录

signals:
    void SigSessionClosed(ClientSession* session);                              // 告诉主服务器：我断开了，请把我从 Map 中移除并销毁

private slots:
    void onReadyRead();                                                         // 核心：粘包/半包拆解逻辑
    void onDisconnected();                                                      // 核心：处理网络异常或正常断开
    void CheckTimeout();                                                        // 定时检查超时的槽函数

private:
    void InitHandlers();                                                        // 注册服务端各种请求的路由处理逻辑

private:
    qintptr m_socketDescriptor;                                                 // 操作系统底层的网络句柄
    QTcpSocket* m_tcpSocket = nullptr;                                          // 真正的通信实体 (将在子线程创建)
    QByteArray m_buffer;                                                        // 数据缓冲区，用于解决 TCP 粘包

    QMap<ServerApi::MsgId, MsgHandler> m_router;                                // O(1) 极速路由分发映射表

    // --- 会话专属的业务数据 ---
    int m_accountId = -1;                                                       // 记录该会话绑定的客户账号 ID
    bool m_isLogined = false;                                                   // 标记该客户端是否已经通过账号验证
    QString m_username = "";                                                    // 记录该会话绑定的客户账号名称

    qint64 m_lastDbUpdateTime = 0;                                              // 记录上次向数据库写入心跳的时间

    QTimer* m_heartbeatTimer = nullptr;                                         // 心跳定时器
    qint64 m_lastRecvTime = 0;                                                  // 上次收到任何数据的时间戳
};