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

    // 分片上传功能
    void SigChunkUploadSuccess(ServerApi::FileType fileType);                                       // 切片上传成功
    void SigChunkUploadFailed(ServerApi::FileType fileType, QString msg);                           // 切片上传失败
    void SigAllChunksAcked(ServerApi::FileType fileType);                                           // 所有切片上传成功

    // 文件分片与海报下载
    void SigCoverDownloaded(ServerApi::FileType fileType, const QString& fileMd5, 
        const QString& localCoverPath);                                                             // 海报下载完成 (参数: 业务类型, 资源的MD5, 保存到本地的绝对路径)
    void SigDownloadFailed(ServerApi::FileType fileType, const QString& errMsg);                    // 分片下载失败 (参数: 业务类型, 错误信息)
    void SigDownloadProgress(ServerApi::FileType fileType, const QString& fileMd5, 
        qint64 chunkSize);                                                                          // 分片下载进度 (参数: 业务类型, 资源的MD5, 刚刚收到的这块切片的字节数)
    void SigDownloadFinished(ServerApi::FileType fileType, const QString& fileMd5);                 // 物理文件彻底下载完成 (参数: 业务类型, 资源的MD5)

    // 影片业务
    void SigMovieUploadSuccess();                                                                   // 影片元数据录入成功
    void SigMovieUploadFailed(QString msg);                                                         // 影片元数据录入失败
    void SigMovieListReceived(const ServerApi::GetMovieListRsp& rsp);                               // 收到影片列表

    // 影片播放历史记录业务相关信号
    void SigRecordsReceived(const ServerApi::GetRecordsRsp& rsp);                                   // 接收到服务器下发的播放记录列表 (用于刷新 UI 表格)
    void SigRecordAdded(const ServerApi::AddRecordRsp& rsp);                                        // 服务器确认新播放记录已成功入库 (对应播放结束后的上报)
    void SigRecordDeleted(const ServerApi::DeleteRecordRsp& rsp);                                   // 服务器确认播放记录已成功删除 (对应用户手动精确删除)

    // 游戏业务信号
    void SigGameListReceived(const ServerApi::GetGameListRsp& rsp);                                 // 收到游戏列表
    void SigGameUploadSuccess();                                                                    // 游戏上传成功
    void SigGameUploadFailed(QString msg);                                                          // 游戏上传失败

    // 商业化/钱包相关信号
    void SigWalletReceived(const ServerApi::GetWalletRsp& rsp);                                     // 钱包数据到账
    void SigGoodsListReceived(const ServerApi::GetGoodsRsp& rsp);                                   // 商品列表到账

    // 支付与商业化业务信号
    void SigOrderCreated(const QString& orderId, const QString& qrUrl, int expireTime);             // 服务端返回订单与二维码
    void SigOrderFailed(int errorCode, const QString& errorMsg);                                    // 订单创建失败信号 (携带具体的错误码和来自服务端的错误消息)
    void SigOrderPaid(const QString& orderId, qint64 newPoints);                                    // 微信异步回调：支付成功！
    void SigFlowRecordsReceived(const ServerApi::GetFlowRsp& rsp);                                  // 资金流水数据到账

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