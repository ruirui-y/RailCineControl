#pragma once
#include <QString>
#include <QByteArray>

class WechatPayCrypto
{
public:
    // 👑 核心：AES-256-GCM 解密微信支付回调的密文
    // apiV3Key: 你在微信商户平台设置的 32 位 APIv3 密钥
    // associatedData: 回调 JSON 中的 resource.associated_data
    // nonce: 回调 JSON 中的 resource.nonce
    // ciphertextB64: 回调 JSON 中的 resource.ciphertext (Base64格式)
    static QString DecryptAES256GCM(const QString& apiV3Key,
        const QString& associatedData,
        const QString& nonce,
        const QString& ciphertextB64);

    // 👑 核心2：生成微信支付 V3 请求的 Authorization 签名头
    // mchid: 商户号
    // serialNo: 商户 API 证书序列号 (在微信商户平台获取)
    // privateKeyPem: 商户私钥内容 (读取 apiclient_key.pem 里的字符串)
    // method: HTTP 方法 (通常为 "POST" 或 "GET")
    // url: 请求相对路径 (如 "/v3/pay/transactions/native")
    // body: 请求报文主体 (若是 GET 请求，传空字符串 "")
    static QString BuildV3Header(const QString& mchid,
        const QString& serialNo,
        const QString& privateKeyPem,
        const QString& method,
        const QString& url,
        const QByteArray& body);
};