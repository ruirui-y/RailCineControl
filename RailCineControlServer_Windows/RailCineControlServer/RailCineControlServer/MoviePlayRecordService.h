#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class MoviePlayRecordService : public std::enable_shared_from_this<MoviePlayRecordService>
{
public:
    MoviePlayRecordService() = default;
    ~MoviePlayRecordService();

    void Init();

private:
    void OnGetRecords(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnAddRecord(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
    void OnDeleteRecord(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);
};