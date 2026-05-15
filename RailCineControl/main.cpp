#include "mainWindow.h"
#include <QtWidgets/QApplication>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTranslator>
#include <QLocale>
#include "Macro.h"
#include "GameItem.h"
#include "LogRecord.h"
#include "Enum.h"
#include "ThreadPool.h"
#include "common.pb.h"
#include "server_msg.pb.h"
#include "JsonTool.h"

void LoadStyle(QApplication* app)
{
    QFile file("./StyleSheet/stylesheet.qss");
    if (file.open(QFile::ReadOnly))
    {
        QString style = QString::fromUtf8(file.readAll());
        app->setStyleSheet(style);
        file.close();
        qDebug() << "Load Style Success";
    }
    else
    {
        qDebug() << "Load Style Failed";
    }
}

void RegisterMetaTypes()
{
    // 1. 注册核心枚举 (必须注册，否则信号槽跨线程传递时会丢失参数)
    qRegisterMetaType<ServerApi::MsgId>("ServerApi::MsgId");
    qRegisterMetaType<ServerApi::FileType>("ServerApi::FileType");                  // 底层引擎文件类型过滤枚举

    // 2. 注册【影片业务】相关响应
    qRegisterMetaType<ServerApi::GetMovieListRsp>("ServerApi::GetMovieListRsp");    // 影片列表
    qRegisterMetaType<ServerApi::DownloadCoverRsp>("ServerApi::DownloadCoverRsp");  // 海报下载
    qRegisterMetaType<ServerApi::UploadMovieRsp>("ServerApi::UploadMovieRsp");      // 影片上传结果

    // 3. 注册【游戏业务】相关响应
    qRegisterMetaType<ServerApi::GetGameListRsp>("ServerApi::GetGameListRsp");      // 游戏列表
    qRegisterMetaType<ServerApi::UploadGameRsp>("ServerApi::UploadGameRsp");        // 游戏上传结果

    // 4. 注册【播放记录】相关响应
    qRegisterMetaType<ServerApi::GetRecordsRsp>("ServerApi::GetRecordsRsp");        // 查询记录结果
    qRegisterMetaType<ServerApi::AddRecordRsp>("ServerApi::AddRecordRsp");          // 添加记录结果
    qRegisterMetaType<ServerApi::DeleteRecordRsp>("ServerApi::DeleteRecordRsp");    // 删除记录结果

    // 5. 注册【登录与上传进度】相关响应
    qRegisterMetaType<ServerApi::LoginRsp>("ServerApi::LoginRsp");                  // 登录响应
    qRegisterMetaType<ServerApi::UploadChunkRsp>("ServerApi::UploadChunkRsp");      // 视频/压缩包分片上传确认

    // 6. 注册【支付与商业化】相关响应 (👑 新增模块)
    qRegisterMetaType<ServerApi::GetWalletRsp>("ServerApi::GetWalletRsp");          // 钱包余额与历史统计响应
    qRegisterMetaType<ServerApi::GetGoodsRsp>("ServerApi::GetGoodsRsp");            // 充值套餐列表响应
    qRegisterMetaType<ServerApi::CreateOrderRsp>("ServerApi::CreateOrderRsp");      // 订单创建与二维码下发响应
    qRegisterMetaType<ServerApi::OrderNotifyPush>("ServerApi::OrderNotifyPush");    // 支付成功异步推送通知

    // 💡 提示：如果信号里传递了列表内的单条数据或结构体，也需要注册以确保跨线程安全
    qRegisterMetaType<ServerApi::MovieInfo>("ServerApi::MovieInfo");
    qRegisterMetaType<ServerApi::PlayRecord>("ServerApi::PlayRecord");
    qRegisterMetaType<ServerApi::GameInfo>("ServerApi::GameInfo");                  // 单条游戏结构体
    qRegisterMetaType<ServerApi::GoodsInfo>("ServerApi::GoodsInfo");                // 单条充值套餐结构体
}

void LoadAppLanguage()
{
    // 1. 读取复用的 LoginConfig.json
    QJsonDocument doc;
    JsonTool::Instance()->readJsonFile(AppConfigPath, doc);
    QJsonObject obj = doc.object();

    // 2. 提取语言配置。如果文件为空或没有 "Language" 字段，默认 fallback 到 "zh"
    QString langCode = "zh";
    if (!obj.isEmpty() && obj.contains("Language")) {
        langCode = obj["Language"].toString();
    }

    // 3. 执行加载逻辑
    if (langCode == "en") 
    {
        // ==========================================================
        // 1. 挂载英文词典 (翻译你的业务代码)
        // ==========================================================
        static QTranslator s_translator;
        QString fullPath = GetLanguageFilePath("en");
        if (QFile::exists(fullPath) && s_translator.load(fullPath)) {
            qApp->installTranslator(&s_translator);
        }

        // ==========================================================
        // 👑 2. 强行把整个应用程序的底层文化环境设为美国英语！
        // 这会瞬间把日历、时间格式全部变成英文原生状态！
        // ==========================================================
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

        qDebug() << "[Language] 成功切换为全英文环境";
    }
}

int main(int argc, char *argv[])
{
    // =================================================================
    // 强心剂 1：强制抛弃 DirectShow，使用 Windows 现代媒体引擎 (WMF)
    // 这会让 Qt 使用和 Windows 自带播放器一模一样的底层解码和渲染管线！
    // =================================================================
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "windowsmediafoundation");

    // =================================================================
    // 强心剂 2：开启 Qt 的全局高分屏与抗锯齿缩放支持
    // 防止 Windows 系统级的野蛮拉伸导致像素狗牙
    // =================================================================
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);

    InitGlobalPaths();                                                                  // 初始化全局路径

    RegisterMetaTypes();                                                                // 注册跨线程通信类型

    // 加载语言
    LoadAppLanguage();

    // 加载样式表
    LoadStyle(&app);

    // 开始记录日志
    LogRecord::startRecord("Log.txt");

    // 启动线程池
    ThreadPool::Instance()->Start(4);

    mainWindow window;

    app.setWindowIcon(QIcon(":/MiNi/Images/MiNiWorld/Login.jpg"));

    app.exec();

    // 停止线程池
    ThreadPool::Instance()->Stop();

    return 0;
}