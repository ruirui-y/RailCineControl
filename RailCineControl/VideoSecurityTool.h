#ifndef VIDEOSECURITYTOOL_H
#define VIDEOSECURITYTOOL_H

#include <QString>
#include <QFile>
#include <QByteArray>
#include <QDebug>

class VideoSecurityTool
{
public:
    // =====================================================================================
    // 在内存中直接对 QByteArray 进行原地 XOR 加解密 (零拷贝，性能拉满)
    // =====================================================================================
    static void XorProcessByteArray(QByteArray& data, const QString& encryptKey)
    {
        if (encryptKey.isEmpty() || data.isEmpty()) return;

        QByteArray keyBytes = encryptKey.toUtf8();
        int keyLen = keyBytes.size();
        if (keyLen == 0) return;                                                                    // 防呆保护

        char* rawData = data.data();
        int dataLen = data.size();

        for (int i = 0; i < dataLen; ++i) 
        {
            // XOR 异或魔法：明文变密文，密文变明文
            rawData[i] ^= keyBytes.at(i % keyLen);
        }
    }

    // =====================================================================================
    // 传入物理路径和密钥，瞬间反转文件前 1MB 的加密状态 (供 PlaybackPage 使用)
    // =====================================================================================
    static bool ToggleEncryption(const QString& filePath, const QString& encryptKey)
    {
        // 如果密钥为空，说明是不需要加密的老视频，直接放行
        if (encryptKey.isEmpty()) return true;

        QFile file(filePath);
        // 必须以 ReadWrite 模式打开
        if (!file.open(QIODevice::ReadWrite)) 
        {
            qDebug() << u8"[Security] ❌ 无法打开文件进行加解密操作:" << filePath;
            return false;
        }

        // 只处理前 1MB
        const int CHUNK_SIZE = 1024 * 1024;
        QByteArray chunkData = file.read(CHUNK_SIZE);

        if (chunkData.isEmpty()) {
            file.close();
            return false;
        }

        // 复用核心引擎：直接调用内存处理函数
        XorProcessByteArray(chunkData, encryptKey);

        // 把磁头拨回文件头，覆写回去！
        file.seek(0);
        file.write(chunkData);
        file.close();

        qDebug() << u8"[Security] 🛡️ 视频物理文件头部状态反转成功!";
        return true;
    }
};

#endif // VIDEOSECURITYTOOL_H