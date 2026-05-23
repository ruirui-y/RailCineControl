#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class AuthService : public std::enable_shared_from_this<AuthService>
{
public:
    AuthService() = default;
    ~AuthService();

    // 初始化：负责向 MsgDispatcher 注册自己
    void Init();

private:
    // 具体的业务处理函数（无状态设计，所有状态全靠传入的 session）
    void OnLogin(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnHeartbeat(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
};