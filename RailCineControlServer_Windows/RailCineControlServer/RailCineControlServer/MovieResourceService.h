#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class MovieResourceService : public std::enable_shared_from_this<MovieResourceService>
{
public:
    MovieResourceService() = default;
    ~MovieResourceService();

    void Init();

private:
    void OnUploadMovie(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnGetMovieList(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
};