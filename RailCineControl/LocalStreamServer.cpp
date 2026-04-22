#include "LocalStreamServer.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <QHash>
#include <QPointer>
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

    QPointer<QTcpSocket> safeSocket(socket);
    QByteArray requestData = safeSocket->readAll();
    QString requestStr = QString::fromUtf8(requestData);

    // 如果不是 GET 请求，直接丢弃
    if (!requestStr.startsWith("GET")) return;

    // 1. 初始化文件与状态上下文 (使用智能指针，Socket销毁时自动关闭文件，杜绝内存泄漏)
    QSharedPointer<StreamState> state = QSharedPointer<StreamState>::create();
    state->file.reset(new QFile(m_currentFilePath));

    if (!state->file->open(QIODevice::ReadOnly)) {
        safeSocket->write("HTTP/1.1 404 Not Found\r\n\r\n");
        safeSocket->disconnectFromHost();
        return;
    }

    qint64 totalSize = state->file->size();
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
        safeSocket->write("HTTP/1.1 416 Range Not Satisfiable\r\n\r\n");
        safeSocket->disconnectFromHost();
        return;
    }

    state->lengthToSend = endRange - startRange + 1;
    state->currentOffset = startRange;
    state->file->seek(startRange);

    // 拼装并发送 HTTP 响应头
    QString responseHeader = QString(
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: %1\r\n"
        "Content-Range: bytes %2-%3/%4\r\n"
        "Connection: close\r\n\r\n"
    ).arg(state->lengthToSend).arg(startRange).arg(endRange).arg(totalSize);

    safeSocket->write(responseHeader.toUtf8());

    // ====================================================================
    // 👑 绝杀：绑定网卡反馈信号 (终极事件驱动引擎)
    // 只要网卡把数据发出去了，就会触发这个 Lambda，我们就在这里接着塞数据
    // ====================================================================
    connect(safeSocket, &QTcpSocket::bytesWritten, safeSocket, [safeSocket, state, this](qint64 /*bytes*/) {
        // 1. 保护机制：如果中途断开，直接退出
        if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState) return;

        // 2. 完成检测：发够了就主动挂断
        if (state->bytesWritten >= state->lengthToSend) {
            safeSocket->disconnectFromHost();
            return;
        }

        // 3. 动态流控：如果当前缓冲区积压超过 5MB，这次就不读硬盘了。
        // 等底层的积压数据发出去后，会自动再次触发 bytesWritten，形成自愈循环！
        if (safeSocket->bytesToWrite() > 5 * 1024 * 1024) return;

        // 4. 读取与解密
        qint64 toRead = qMin((qint64)(64 * 1024), state->lengthToSend - state->bytesWritten);
        QByteArray chunk = state->file->read(toRead);

        if (chunk.isEmpty()) {
            safeSocket->disconnectFromHost();
            return;
        }

        DecryptStreamChunk(chunk, state->currentOffset);

        // 5. 写入 Socket (这句 write 会在未来网卡发完时，再次触发当前的 Lambda！)
        safeSocket->write(chunk);

        state->bytesWritten += chunk.size();
        state->currentOffset += chunk.size();
        });

    // ====================================================================
    // 🚀 启动抽水泵：手动触发第一次发送，启动整个“永动机”循环！
    // ====================================================================
    qint64 initialRead = qMin((qint64)(64 * 1024), state->lengthToSend);
    QByteArray firstChunk = state->file->read(initialRead);
    DecryptStreamChunk(firstChunk, state->currentOffset);
    safeSocket->write(firstChunk); // 这句写入后，网卡一旦消化完，就会触发上面绑定的 Lambda

    state->bytesWritten += firstChunk.size();
    state->currentOffset += firstChunk.size();

    // onReadyRead 函数瞬间结束，将 CPU 完全交还给 Qt 事件循环！
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