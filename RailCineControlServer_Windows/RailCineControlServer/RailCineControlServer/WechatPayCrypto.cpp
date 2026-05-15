#include "WechatPayCrypto.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <QDateTime>
#include <QUuid>
#include <QDebug>

QString WechatPayCrypto::DecryptAES256GCM(const QString& apiV3Key, const QString& associatedData, const QString& nonce, const QString& ciphertextB64)
{
    // 1. Base64 解码密文
    QByteArray cipherWithTag = QByteArray::fromBase64(ciphertextB64.toUtf8());
    if (cipherWithTag.length() <= 16) {
        qDebug() << u8"[Crypto] 密文长度错误！";
        return "";
    }

    // 微信的机制：末尾 16 字节是 Auth Tag，前面的是真正的密文
    int cipherLen = cipherWithTag.length() - 16;
    QByteArray ciphertext = cipherWithTag.left(cipherLen);
    QByteArray tag = cipherWithTag.right(16);

    QByteArray key = apiV3Key.toUtf8();
    QByteArray iv = nonce.toUtf8();
    QByteArray aad = associatedData.toUtf8();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    QByteArray plaintext;
    plaintext.resize(cipherLen); // 明文长度绝不会超过密文
    int outLen = 0;
    int plainLen = 0;

    // 2. 初始化解密上下文 (指定 AES-256-GCM)
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length(), nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, (unsigned char*)key.data(), (unsigned char*)iv.data());

    // 3. 附加关联数据 (AAD)
    if (aad.length() > 0) {
        EVP_DecryptUpdate(ctx, nullptr, &outLen, (unsigned char*)aad.data(), aad.length());
    }

    // 4. 提供密文
    EVP_DecryptUpdate(ctx, (unsigned char*)plaintext.data(), &outLen, (unsigned char*)ciphertext.data(), ciphertext.length());
    plainLen = outLen;

    // 5. 提供 Auth Tag 供校验
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.length(), (void*)tag.data());

    // 6. 结束解密并校验 Tag
    int ret = EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext.data() + outLen, &outLen);
    EVP_CIPHER_CTX_free(ctx);

    if (ret > 0) {
        plainLen += outLen;
        plaintext.resize(plainLen);
        return QString::fromUtf8(plaintext); // 成功返回明文 JSON 字符串！
    }
    else {
        qDebug() << u8"[Crypto] AES-GCM 解密失败，Auth Tag 校验不通过或密钥错误！";
        return "";
    }
}

QString WechatPayCrypto::BuildV3Header(const QString& mchid, const QString& serialNo, const QString& privateKeyPem, const QString& method, const QString& url, const QByteArray& body)
{
    // 1. 生成微信需要的时间戳和随机串
    QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());

    // 生成 32 位随机字符串 (去掉 UUID 的横杠和大括号)
    QString nonce = QUuid::createUuid().toString().remove("-").remove("{").remove("}");

    // 2. 严格按照微信官方格式拼接签名串 (\n 不能少！)
    // 格式: HTTP请求方法\n URL\n 请求时间戳\n 请求随机串\n 请求报文主体\n
    QString signMessage = method + "\n"
        + url + "\n"
        + timestamp + "\n"
        + nonce + "\n"
        + QString::fromUtf8(body) + "\n";

    QByteArray msgBytes = signMessage.toUtf8();

    // ==========================================================
    // 3. OpenSSL 核心流程：使用 RSA-SHA256 私钥签名
    // ==========================================================

    // A. 从内存字符串读取 PEM 格式的私钥
    QByteArray keyBytes = privateKeyPem.toUtf8();
    BIO* keyBio = BIO_new_mem_buf(keyBytes.data(), keyBytes.length());
    if (!keyBio) {
        qDebug() << u8"[Crypto] 创建 BIO 失败";
        return "";
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
    BIO_free(keyBio); // 释放 BIO 内存

    if (!pkey) {
        qDebug() << u8"[Crypto] 加载商户私钥失败！请检查 privateKeyPem 格式是否正确";
        return "";
    }

    // B. 初始化签名上下文
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return "";
    }

    QByteArray signatureBytes;

    // C. 执行 SHA256 签名
    if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1)
    {
        if (EVP_DigestSignUpdate(mdctx, msgBytes.data(), msgBytes.length()) == 1)
        {
            size_t sigLen = 0;
            // 第一次调用获取签名所需的缓冲区大小
            if (EVP_DigestSignFinal(mdctx, nullptr, &sigLen) == 1 && sigLen > 0)
            {
                signatureBytes.resize(sigLen);
                // 第二次调用真正写入签名数据
                EVP_DigestSignFinal(mdctx, (unsigned char*)signatureBytes.data(), &sigLen);
                signatureBytes.resize(sigLen); // 修正为实际长度
            }
        }
    }

    // D. 释放 OpenSSL 内存
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    if (signatureBytes.isEmpty()) {
        qDebug() << u8"[Crypto] 生成 RSA 签名失败";
        return "";
    }

    // 4. 将二进制签名结果进行 Base64 编码
    QString signatureB64 = signatureBytes.toBase64();

    // 5. 组装最终发给微信的 Authorization Header
    QString authHeader = QString("WECHATPAY2-SHA256-RSA2048 "
        "mchid=\"%1\",nonce_str=\"%2\",signature=\"%3\",timestamp=\"%4\",serial_no=\"%5\"")
        .arg(mchid)
        .arg(nonce)
        .arg(signatureB64)
        .arg(timestamp)
        .arg(serialNo);

    return authHeader;
}