#pragma once
#include <memory>
#include <QByteArray>
#include "server_msg.pb.h"

class ClientSession;

class FileTransferService : public std::enable_shared_from_this<FileTransferService>
{
public:
    FileTransferService() = default;
    ~FileTransferService();

    void Init();

private:
    // 分片上传逻辑
    void OnUploadChunk(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);

    // 分片下载逻辑
    void OnDownloadChunk(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);

    // 海报下载逻辑
    void OnDownloadCover(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData);

    // 辅助逻辑：内部落盘封装 (不再依赖 ClientSession 内部方法)
    void SaveChunkToDisk(std::shared_ptr<ClientSession> session, std::shared_ptr<ServerApi::UploadChunkReq> req, uint64_t seq_id, const QString& filePath);
};