#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class GameResourceService : public std::enable_shared_from_this<GameResourceService>
{
public:
    GameResourceService() = default;
    ~GameResourceService();

    void Init();

private:
    void OnUploadGame(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnGetGameList(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
};