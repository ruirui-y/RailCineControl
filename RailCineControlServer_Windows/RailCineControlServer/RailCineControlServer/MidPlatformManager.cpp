#include "MidPlatformManager.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QUrl>
#include <QDebug>

MidPlatformManager::MidPlatformManager(QObject* parent) : QObject(parent)
{
    // 无状态类，不需要初始化任何成员变量
}

MidPlatformManager::~MidPlatformManager()
{
}

QString MidPlatformManager::GetAccessToken()
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("https://api.stg.playlink.games/connect/token"));

    // 严格对齐文档的 Header
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Language", "zh-Hans");

    // urlencoded 格式的 Body
    QByteArray body = "grant_type=client_credentials&scope=OpenApi"
        "&client_id=3a214bed97c0b7222f62e6df4d2f7993"
        "&client_secret=3a214bed97bec75bfe8bbc46e4262b60";

    // 发起 POST 请求
    QNetworkReply* reply = manager.post(request, body);

    // 👑 局部事件循环：把 Qt 的异步请求变成完美的同步阻塞，契合你的架构
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString token;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument resDoc = QJsonDocument::fromJson(reply->readAll());
        token = resDoc.object()["access_token"].toString();
        qDebug() << u8"✅ [QtNetwork] 获取 Token 成功！Token 长度:" << token.length();
    }
    else {
        qDebug() << u8"❌ [QtNetwork] 获取 Token 失败！ HTTP Code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << u8"详情:" << reply->errorString();
    }

    reply->deleteLater();
    return token;
}

int MidPlatformManager::CheckOrderFromMidPlatform(const QString& orderId)
{
    // 1. 直接调用本类的 GetAccessToken()
    QString accessToken = GetAccessToken();

    if (accessToken.isEmpty()) {
        qDebug() << u8"❌ [中台API] 查单失败：无法获取 AccessToken，终止查单。";
        return -1;
    }

    // 2. 组装 GET 请求
    QNetworkAccessManager manager;
    QString urlStr = QString("https://api.stg.playlink.games/open/transactions/%1").arg(orderId);
    QNetworkRequest request((QUrl(urlStr)));

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    // 注入鉴权头
    request.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());

    // 3. 发送 GET 请求并阻塞等待
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    int resultStatus = -1;

    // 4. 解析响应
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument resDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject resObj = resDoc.object();

        QString code = resObj["code"].toString();
        QString tradeStatus = resObj["data"].toObject()["status"].toString();
        if (tradeStatus.isEmpty()) tradeStatus = resObj["status"].toString();

        if (tradeStatus == "SUCCESS" || tradeStatus == "PAID") {
            resultStatus = 1;
        }
        else if (tradeStatus == "WAITING" || tradeStatus == "PENDING" || tradeStatus == "NOTPAY") {
            resultStatus = 0;
        }
        else {
            resultStatus = -1;
        }

        qDebug() << u8"🔍 [中台API] 查单完成，订单:" << orderId << " 中台状态:" << tradeStatus;
    }
    else {
        qDebug() << u8"❌ [中台API] 查单网络报错，HTTP Code:"
            << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << u8"详情:" << reply->errorString();
    }

    reply->deleteLater();
    return resultStatus;
}