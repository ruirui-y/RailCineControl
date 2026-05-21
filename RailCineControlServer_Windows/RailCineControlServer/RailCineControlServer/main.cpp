#include <QtWidgets/QApplication>
#include "WorkerThread.h"
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include "Global.h"
#include "MainWindow.h"
#include "ThreadPool.h"
#include "LogRecord.h"

void RegisterMetaTypes()
{
    qRegisterMetaType<QueryCallback>("QueryCallback");
    qRegisterMetaType<UpdateCallback>("UpdateCallback");
    qRegisterMetaType<TransactionCallback>("TransactionCallback");
}

int main(int argc, char* argv[])
{
    qputenv("QT_LOGGING_RULES", "qt.network.monitor.warning=false");
    QApplication app(argc, argv);

    QString currDir = QDir::currentPath();
    // 初始化全局配置
    if (!GlobalConfig::Instance()->Init(currDir)){
        qWarning() << u8"严重错误：配置文件加载失败，服务器即将退出！";
        return -1;
    }

    // 注册跨线程信号
    RegisterMetaTypes();

    // 注册日志
    LogRecord::InitLog("ControlHubServer.txt");

    // 启动线程池
    ThreadPool::Instance()->Start(4);

    MainWindow w;
    w.show();

    app.exec();

    // 停止线程池
    ThreadPool::Instance()->Stop();

    return 0;
}