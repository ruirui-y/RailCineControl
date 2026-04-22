#ifndef TCPMGR_H
#define TCPMGR_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QMap>
#include <functional>     
#include "common.pb.h"          
#include "server_msg.pb.h"      

// =========================================================================================
// 全新路由回调定义：
// 直接传入解析好的 Header (用于获取 seq_id 和状态) 以及待解析的 Body 二进制流
// =========================================================================================
using MsgHandler = std::function<void(const ServerApi::PacketHeader& header, const QByteArray& bodyData)>;

class TCPMgr : public QObject
{
    Q_OBJECT

public:
    TCPMgr(QObject* parent = nullptr);
    ~TCPMgr();
    void Login(QString account, QString password);
    void StartHeartBeat();
    void StopHeartBeat();
    void SetInitiateDisCon(bool bDisCon) { m_bIsInitiateDisCon = bDisCon; }
    void AccountLoginOut();

    // 泛型发送接口：只要传入 MsgId 和任意 Protobuf Message，自动封包并发送
    void SendProtoMsg(ServerApi::MsgId msgId, const google::protobuf::Message& protoMsg, uint64_t seqId = 0);

public slots:
    void SlotTcpConnect();

signals:
    // =====================================================================================
    // UI 交互信号
    // =====================================================================================
    void SigConnectSuccess(bool bSuccess);                                                          // 连接成功，触发登录请求
    void SigConnectClose();                                                                         // 连接断开

    // 登录与业务信号
    void SigLoginSuccess();                                                                         // 登陆成功
    void SigLoginFailed(int errorCode, QString msg);                                                // 登陆失败 (带上 Protobuf 传回来的错误码和提示)

    // 影片上传业务
    void SigChunkUploadSuccess();                                                                   // 切片上传成功，上传下一个切片
    void SigAllChunksAcked();                                                                       // 所有切片均已得到服务端确认
    void SigUploadFailed(QString msg);
    void SigUploadSuccess();

    // 拉取影片
    void SigMovieListReceived(const ServerApi::GetMovieListRsp& rsp);                               // 收到影片列表
    void SigCoverDownloaded(const QString& fileMd5, const QString& localCoverPath);                 // 海报下载完成 (参数: 影片的MD5, 保存到本地的绝对路径)
    void SigDownloadFailed(const QString& errMsg);                                                  // 视频分片下载失败 (参数: 错误信息)
    void SigDownloadProgress(const QString& fileMd5, qint64 chunkSize);                             // 视频分片下载进度 (参数: 影片的MD5, 刚刚收到的这块切片的字节数)
    void SigDownloadFinished(const QString& fileMd5);                                               // 视频彻底下载完成 (参数: 影片的MD5)

    // 影片播放历史记录业务相关信号
    void SigRecordsReceived(const ServerApi::GetRecordsRsp& rsp);                                   // 接收到服务器下发的播放记录列表 (用于刷新 UI 表格)
    void SigRecordAdded(const ServerApi::AddRecordRsp& rsp);                                        // 服务器确认新播放记录已成功入库 (对应播放结束后的上报)
    void SigRecordDeleted(const ServerApi::DeleteRecordRsp& rsp);                                   // 服务器确认播放记录已成功删除 (对应用户手动精确删除)

private:
    void InitTcpSocket();                                                                           // 初始化TCP套接字
    void InitHearbeatTimer();                                                                       // 初始化心跳定时器
    void InitHandlers();                                                                            // 注册所有 MsgId 对应的处理 Lambda

private slots:
    void onReadyRead();                                                                             // 替换原来的 HandleMsg，核心的 TCP 拆包引擎
    void onHeartbeatTick();                                                                         // 心跳触发槽函数

private:
    QTcpSocket* m_TcpSocket = nullptr;
    QTimer* m_hearbeatTimer = nullptr;

    bool m_bIsInitiateDisCon = false;
    QString m_Host;
    int m_Port;

    // 解决 TCP 粘包的核心缓冲区（淘汰了以前的 m_MessageID 等散装变量）
    QByteArray m_buffer;

    // 终极 O(1) 路由表
    QMap<ServerApi::MsgId, MsgHandler> m_router;
};

#endif // TCPMGR_H