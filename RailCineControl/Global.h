#ifndef GLOBAL_H
#define GLOBAL_H
#include <QWidget>
#include <functional>
#include <QRegularExpression>
#include "QStyle"

#include <memory>
#include <iostream>
#include <mutex>

#include <QString>
#include <QSettings>
#include <vector>

using namespace std;


// extern 声明此变量是在其他文件中定义的全局变量
extern function<void(QWidget*)> repolish;

extern function<QString(QString)> xorString;			                                                        // 密码加密

extern QString ConfigPath;								                                                        // 配置文件路径
extern QSettings* ConfigSettings;						                                                        // 配置文件对象

extern QString ClientConfigPath;                                                                                // 客户端配置文件路径
extern QString LoginConfigPath;                                                                                 // 登录配置文件路径

// 系统全局配置文件路径 (存语言、网络设置、硬件配置等)
extern QString AppConfigPath;

// 翻译文件所在目录
extern QString TranslationsPath;
extern QString GetLanguageFilePath(const QString& lang_code);                                                   // 获取语言文件路径

// 影片配置文件路径
extern QString MovieConfigPath;

// 影片海报路径
extern QString MovieCoverPath;

// 影片视频路径
extern QString MovieVideoPath;

// 影片播放记录配置文件路径
extern QString MovieRecordPath;

extern const int tipOffset;								                                                        // 提示框偏移量

using DeferFunc = std::function<void()>;
class Defer
{
public:
    Defer(DeferFunc func) : m_func(func) {}
    ~Defer() { m_func(); }
private:
    DeferFunc m_func;
};
#endif // GLOBAL_H