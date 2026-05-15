#include "Global.h"
#include "JsonTool.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDebug>

bool GlobalConfig::Init(const QString& appDirPath)
{
    m_configPath = appDirPath + "/Config/server_config.json";

    QJsonDocument doc;
    QString errStr;
    if (!JsonTool::Instance()->readJsonFile(m_configPath, doc, &errStr)) {
        qDebug() << u8"[GlobalConfig] 读取配置文件失败:" << errStr;
        return false;
    }

    QJsonObject root = doc.object();

    // 解析 Server 节点
    if (root.contains("Server")) {
        QJsonObject serverObj = root["Server"].toObject();
        m_tcpPort = serverObj["TcpPort"].toInt(5486);
        m_httpPort = serverObj["HttpPort"].toInt(8182);
    }

    // 解析 WechatPay 节点
    if (root.contains("WechatPay")) {
        QJsonObject wxObj = root["WechatPay"].toObject();
        m_wxMchId = wxObj["MchId"].toString();
        m_wxAppId = wxObj["AppId"].toString();                                              // AppID
        m_wxNotifyUrl = wxObj["NotifyUrl"].toString();                                      // NotifyUrl
        m_wxSerialNo = wxObj["SerialNo"].toString();
        m_wxPrivateKey = wxObj["PrivateKey"].toString();
    }

    return true;
}