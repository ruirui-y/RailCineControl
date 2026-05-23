#pragma once

#include <QMap>
#include <QObject>
#include <functional>
#include <memory>
#include <QDebug>
#include "singletion.h"                                                 
#include "server_msg.pb.h"                                              

class ClientSession;                                                    

// 定义标准的消息处理器签名 (形参：会话智能指针, 包头, 包体)
using MsgHandler = std::function<void(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader&, const QByteArray&)>;

class MsgDispatcher : public QObject, public Singleton<MsgDispatcher>
{
    Q_OBJECT

public:
    friend class Singleton<MsgDispatcher>;

public:
    ~MsgDispatcher() = default;

public:
    // 注册路由 (建议在 Server 启动前的单线程环境中统一注册)
    void RegisterRoute(ServerApi::MsgId msgId, MsgHandler handler);

    // 分发消息 (供 ClientSession 收到数据后调用)
    void Dispatch(std::shared_ptr<ClientSession> session, ServerApi::MsgId msgId, const ServerApi::PacketHeader& header, const QByteArray& body);

private:
    MsgDispatcher(QObject* parent = 0) {}
    
    // 核心路由表：MsgId -> 业务处理函数的映射
    QMap<ServerApi::MsgId, MsgHandler> m_router;
};