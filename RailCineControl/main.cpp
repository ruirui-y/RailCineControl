#include "mainWindow.h"
#include <QtWidgets/QApplication>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QThread>
#include "Global.h"
#include "Macro.h"
#include "GameItem.h"
#include "LogRecord.h"
#include "Struct.h"
#include "Enum.h"
#include "ThreadPool.h"
#include <QVBoxLayout>


void LoadStyle(QApplication* app)
{
    QFile file(":/StyleSheet/stylesheet.qss");
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
    qRegisterMetaType<ReqID_TCP>("ReqID_TCP");
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

    RegisterMetaTypes();
    QApplication app(argc, argv);

    // 启动线程池
    ThreadPool::Instance()->Start(4);

    mainWindow window;

    app.setWindowIcon(QIcon(":/MiNi/Images/MiNiWorld/Login.jpg"));

    // 加载样式表
    LoadStyle(&app);

    return app.exec();
}