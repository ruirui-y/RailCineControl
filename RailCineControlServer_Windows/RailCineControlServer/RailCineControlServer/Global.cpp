#include "Global.h"
#include "JsonTool.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDebug>

bool GlobalConfig::Init(const QString& appDirPath)
{
    // 1. 拼接绝对路径
    m_configPath = appDirPath + "/Config/server_config.json";

    // 2. 利用 JsonTool 读取文件
    QJsonDocument doc;
    QString errStr;
    if (!JsonTool::Instance()->readJsonFile(m_configPath, doc, &errStr)) {
        qDebug() << u8"[GlobalConfig] ❌ 读取配置文件失败:" << errStr << u8"路径:" << m_configPath;
        return false;
    }

    if (!doc.isObject()) {
        qDebug() << u8"[GlobalConfig] ❌ 配置文件格式错误，非 JSON 对象！";
        return false;
    }

    QJsonObject root = doc.object();

    // 3. 解析 Server 节点
    if (root.contains("Server")) {
        QJsonObject serverObj = root["Server"].toObject();
        if (serverObj.contains("TcpPort")) m_tcpPort = serverObj["TcpPort"].toInt(5486);
        if (serverObj.contains("HttpPort")) m_httpPort = serverObj["HttpPort"].toInt(8182);
    }

    // 4. 解析 WechatPay 节点
    if (root.contains("WechatPay")) {
        QJsonObject wxObj = root["WechatPay"].toObject();
        m_wxMchId = wxObj["MchId"].toString();
        m_wxSerialNo = wxObj["SerialNo"].toString();
        m_wxPrivateKey = wxObj["PrivateKey"].toString();
    }

    qDebug() << u8"[GlobalConfig] ✅ 全局配置加载成功! TCP:" << m_tcpPort << u8" HTTP:" << m_httpPort;
    return true;
}