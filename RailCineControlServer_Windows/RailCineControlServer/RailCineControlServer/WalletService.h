#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class WalletService : public std::enable_shared_from_this<WalletService>
{
public:
    WalletService() = default;
    ~WalletService();

    void Init();

private:
    void OnGetWallet(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnGetGoods(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnGetFlow(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
};