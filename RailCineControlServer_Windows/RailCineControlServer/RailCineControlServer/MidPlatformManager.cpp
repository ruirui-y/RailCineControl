#include "MidPlatformManager.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QUrl>
#include <QDebug>
#include "Enum.h"

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
    QString accessToken = GetAccessToken();
    if (accessToken.isEmpty()) {
        qDebug() << u8"❌ [中台API] 查单失败：无法获取 AccessToken";
        return PAY_STATUS_ERROR;
    }

    QNetworkAccessManager manager;
    QString urlStr = QString("https://api.stg.playlink.games/open/transactions/%1").arg(orderId);
    QNetworkRequest request((QUrl(urlStr)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    int resultStatus = PAY_STATUS_UNPAID;                                                                           // 初始化未支付

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument resDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject resObj = resDoc.object();

        // 👑 按照您提供的最新 JSON 结构精准提取
        QString outTradeNo = resObj["outTradeNo"].toString();
        QString transactionNo = resObj["transactionNo"].toString();
        resultStatus = resObj["paymentStatus"].toInt();

        // 容错兜底：如果中台抽风没返回订单号，用我们查单时传进去的
        if (outTradeNo.isEmpty()) outTradeNo = orderId;

        qDebug() << u8"🔍 [中台API] 查单完成，订单:" << outTradeNo << u8" 状态码:" << resultStatus;

        // =========================================================================
        // 👑 终极精髓：直接把结果当做信号发射出去！让 TcpServer 的 OnPaymentResult 去干活！
        // =========================================================================
        emit SigPaymentResult(outTradeNo, transactionNo, resultStatus);
    }
    else {
        // 这里说明大概率该订单没有支付
        QByteArray errorBody = reply->readAll();
        qDebug() << u8"❌ [中台API] 查单网络报错，HTTP Code:"
            << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << u8"详情:" << reply->errorString()
            << u8"中台返回的详细拒绝原因:" << errorBody; // 盯紧这句话！
    }

    reply->deleteLater();

    // 依然把状态 return 出去，这对清理任务有大用！
    return resultStatus;
}

// 获取流水号
QString MidPlatformManager::FetchTransactionId(const QString& orderId)
{
    QString accessToken = GetAccessToken();
    if (accessToken.isEmpty()) {
        qDebug() << u8"❌ [中台API] 获取流水号失败：无法获取 AccessToken";
        return QString(); // 返回空字符串
    }

    QNetworkAccessManager manager;
    QString urlStr = QString("https://api.stg.playlink.games/open/transactions/%1").arg(orderId);
    QNetworkRequest request((QUrl(urlStr)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString transactionId;

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument resDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject resObj = resDoc.object();

        // 提取流水号
        transactionId = resObj["transactionNo"].toString();

        if (!transactionId.isEmpty()) {
            qDebug() << u8"✅ [中台API] 成功补救拉取到流水号:" << transactionId;
        }
    }
    else {
        qDebug() << u8"❌ [中台API] 补救拉取流水号网络报错，HTTP Code:"
            << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << u8"详情:" << reply->errorString();
    }

    reply->deleteLater();
    return transactionId;
}