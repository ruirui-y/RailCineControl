#include "LocalStreamServer.h"
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QHash>
#include <QDebug>


bool LocalStreamServer::StartServer(quint16 port)
{
    m_port = port;
    if (m_server->isListening()) return true;

    // 仅监听本地环回地址 (127.0.0.1)，绝对切断外部物理网络访问
    if (m_server->listen(QHostAddress::LocalHost, m_port)) {
        connect(m_server, &QTcpServer::newConnection, this, &LocalStreamServer::onNewConnection);
        qDebug() << u8"🚀 [LocalStream] 本地暗网流媒体引擎启动成功! 端口:" << m_port;
        return true;
    }
    qDebug() << u8"❌ [LocalStream] 本地服务器启动失败:" << m_server->errorString();
    return false;
}

void LocalStreamServer::SetCurrentMedia(const QString& filePath, const QString& encryptKey)
{
    m_currentFilePath = filePath;
    m_currentEncryptKey = encryptKey;
    qDebug() << "m_currentFilePath = " << m_currentFilePath << " m_currentEncryptKey = " << m_currentEncryptKey;
}

QString LocalStreamServer::GetPlayUrl() const
{
    // 生成欺骗播放器的虚假 URL
    return QString("http://127.0.0.1:%1/play_secure.mp4").arg(m_port);
}

void LocalStreamServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &LocalStreamServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &LocalStreamServer::onSocketDisconnected);
    }
}

void LocalStreamServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray requestData = socket->readAll();
    QString requestStr = QString::fromUtf8(requestData);

    // 如果不是 GET 请求，直接丢弃
    if (!requestStr.startsWith("GET")) return;

    QFile file(m_currentFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    qint64 totalSize = file.size();
    qint64 startRange = 0;
    qint64 endRange = totalSize - 1;

    // 解析 HTTP Range 头部 (极其重要！播放器拖拽进度条全靠这个)
    int rangeIdx = requestStr.indexOf("Range: bytes=");
    if (rangeIdx != -1) {
        int endLineIdx = requestStr.indexOf("\r\n", rangeIdx);
        QString rangeStr = requestStr.mid(rangeIdx + 13, endLineIdx - rangeIdx - 13);
        QStringList parts = rangeStr.split("-");
        startRange = parts[0].toLongLong();
        if (parts.size() > 1 && !parts[1].isEmpty()) {
            endRange = parts[1].toLongLong();
        }
    }

    // 边界安全校验
    if (startRange >= totalSize || endRange >= totalSize) {
        socket->write("HTTP/1.1 416 Range Not Satisfiable\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    qint64 lengthToSend = endRange - startRange + 1;

    // 拼装标准的 HTTP 206 视频流响应头
    QString responseHeader = QString(
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: %1\r\n"
        "Content-Range: bytes %2-%3/%4\r\n"
        "Connection: close\r\n\r\n"
    ).arg(lengthToSend).arg(startRange).arg(endRange).arg(totalSize);

    socket->write(responseHeader.toUtf8());
    // 核心：把文件磁头拨到播放器想要的位置
    file.seek(startRange);
    qint64 bytesWritten = 0;
    qint64 currentOffset = startRange;                                                  // 记录绝对偏移量，用于加密错位计算！

    // 开始抽水！每次读取 64KB 发送，防止内存撑爆
    const int BUF_SIZE = 64 * 1024;
    const qint64 HIGH_WATER_MARK = 5 * 1024 * 1024;

    while (bytesWritten < lengthToSend) {
        // 如果播放器中途断开，立刻刹车
        if (socket->state() != QAbstractSocket::ConnectedState) break;

        // ====================================================================
        // 👑 真正的商业级流控：只要底层缓冲区没满 5MB，就疯狂往里塞数据！
        // 满了 5MB，说明播放器吃不消了，我们再稍作休眠等待。
        // ====================================================================
        if (socket->bytesToWrite() > HIGH_WATER_MARK) {
            socket->waitForBytesWritten(10); // 稍微等一下，让网卡把数据发出去
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            continue; // 继续检查水位，不要去读硬盘
        }

        qint64 toRead = qMin((qint64)BUF_SIZE, lengthToSend - bytesWritten);
        QByteArray chunk = file.read(toRead);
        if (chunk.isEmpty()) break;

        // 在管道口，进行瞬间解密！
        DecryptStreamChunk(chunk, currentOffset);

        socket->write(chunk);

        bytesWritten += chunk.size();
        currentOffset += chunk.size(); // 偏移量递增
    }

    file.close();
    // 数据发送完毕，挂断这个短连接
    socket->disconnectFromHost();
}

void LocalStreamServer::onSocketDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) socket->deleteLater();
}

// =======================================================================
// 高阶流式 XOR 引擎 (带物理偏移量，完美兼容视频随意拖拽)
// =======================================================================
void LocalStreamServer::DecryptStreamChunk(QByteArray& data, qint64 globalOffset)
{
    if (m_currentEncryptKey.isEmpty() || data.isEmpty()) return;

    QByteArray keyBytes = m_currentEncryptKey.toUtf8();
    int keyLen = keyBytes.size();
    if (keyLen == 0) return;

    char* rawData = data.data();
    int dataLen = data.size();

    for (int i = 0; i < dataLen; ++i) {
        qint64 absolutePos = globalOffset + i;

        // 💡 只有属于前 1MB (1048576 字节) 的数据才是密文，才需要解密！
        if (absolutePos < 1048576) {
            // 🚨 最核心的一行：绝对偏移量取模，保证哪怕中途截断读取，密钥也不会错位！
            rawData[i] ^= keyBytes.at(absolutePos % keyLen);
        }
        else {
            // 超过 1MB 区域的全是明文，直接跳出循环，节省 CPU 算力！
            break;
        }
    }
}