#ifndef LOCALSTREAMSERVER_H
#define LOCALSTREAMSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "singletion.h"

class LocalStreamServer : public QObject, public Singleton<LocalStreamServer>
{
    Q_OBJECT
public:
    friend class Singleton<LocalStreamServer>;

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
    LocalStreamServer(QObject* parent = nullptr) : QObject(parent), m_server(new QTcpServer(this)) {}
    ~LocalStreamServer() { m_server->close(); }

    // 核心流式异或引擎 (带绝对偏移量矫正，防花屏补丁)
    void DecryptStreamChunk(QByteArray& data, qint64 globalOffset);

private:
    QTcpServer* m_server;
    QString m_currentFilePath;
    QString m_currentEncryptKey;
    quint16 m_port = 12345;
};

#endif // LOCALSTREAMSERVER_H