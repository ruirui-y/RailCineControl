#ifndef LOCALSTREAMSERVER_H
#define LOCALSTREAMSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSharedPointer>
#include <QFile>

// =========================================================================================
// 流媒体发送状态上下文 (Stream Context)
// 核心设计：被 QSharedPointer 包装后按值传入 Lambda。
// 作用：利用智能指针的引用计数，跨越多次异步网卡事件共享状态，并在发送完毕后自动回收文件句柄。
// =========================================================================================
struct StreamState
{
    QSharedPointer<QFile> file;         // 核心句柄：视频文件对象。只要 Lambda 引用计数不归零，文件就不会被系统关闭或销毁
    qint64 bytesWritten = 0;            // 进度累加器：当前已发给网卡的字节数 (当它 >= lengthToSend 时，触发断开)
    qint64 lengthToSend = 0;            // 发送目标值：本次 HTTP 206 请求约定要发送的总字节数 (EndRange - StartRange + 1)
    qint64 currentOffset = 0;           // 绝对磁头位置：当前在原文件中的物理偏移量 (极其重要：用于 XOR 异或解密时的错位对齐计算)
};

class LocalStreamServer : public QObject
{
    Q_OBJECT

public:
    LocalStreamServer(QObject* parent = nullptr) : QObject(parent), m_server(new QTcpServer(this)) {}
    ~LocalStreamServer() { m_server->close(); }

public:
    // 启动本地暗网服务器，默认监听 12345 端口
    bool StartServer(quint16 port = 12345);

    // 告诉服务器：准备播放这个文件，使用这把钥匙
    void SetCurrentMedia(const QString& filePath, const QString& encryptKey);

    // 获取用来喂给 QMediaPlayer 的本地魔改网址
    QString GetPlayUrl() const;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    // 核心流式异或引擎 (带绝对偏移量矫正，防花屏补丁)
    void DecryptStreamChunk(QByteArray& data, qint64 globalOffset);

private:
    QTcpServer* m_server;
    QString m_currentFilePath;
    QString m_currentEncryptKey;
    quint16 m_port = 12345;
};

#endif // LOCALSTREAMSERVER_H