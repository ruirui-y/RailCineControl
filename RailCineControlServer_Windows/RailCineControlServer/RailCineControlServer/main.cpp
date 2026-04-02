#include <QtWidgets/QApplication>
#include "WorkerThread.h"
#include <QDateTime>
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
    RegisterMetaTypes();

    // 注册日志
    LogRecord::InitLog("ControlHubServer.txt");

    // 启动线程池
    ThreadPool::Instance()->Start(4);

    MainWindow w;
    w.show();

    return app.exec();
}