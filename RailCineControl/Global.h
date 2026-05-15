#ifndef GLOBAL_H
#define GLOBAL_H

#include <QWidget>
#include <functional>
#include <QRegularExpression>
#include <QStyle>
#include <memory>
#include <iostream>
#include <mutex>
#include <QString>
#include <QSettings>
#include <vector>

using namespace std;

// ========================================================
// 1. 核心工具与函数接口
// ========================================================
extern const int tipOffset;                                                     // 提示框的全局像素偏移量
extern function<void(QWidget*)> repolish;                                       // 刷新 QSS 样式的闭包函数
extern function<QString(QString)> xorString;                                    // 基础字符串异或加密函数
extern QString GetLanguageFilePath(const QString& lang_code);                   // 动态获取对应语言 .qm 文件的绝对路径

void InitGlobalPaths();                                                         // 👑 初始化所有全局路径 (必须在 QApplication 之后调用)


// ========================================================
// 2. [系统配置] 路径与句柄
// ========================================================
extern QString ConfigPath;                                                      // [文件] 主程序 Config.ini 绝对路径
extern QSettings* ConfigSettings;                                               // [句柄] 全局读写 Config.ini 的操作对象

extern QString ClientConfigPath;                                                // [文件] 网络节点 config.json 绝对路径
extern QString LoginConfigPath;                                                 // [文件] 登录凭证 login.json 绝对路径
extern QString AppConfigPath;                                                   // [文件] UI偏好设置 app_settings.json 绝对路径


// ========================================================
// 3. [国际化翻译] 路径
// ========================================================
extern QString TranslationsPath;                                                // [目录] 多语言 .qm 文件存放目录


// ========================================================
// 4. [影片流媒体] 路径
// ========================================================
extern QString MovieConfigPath;                                                 // [文件] 影片库清单 movies.json 绝对路径
extern QString MovieCoverPath;                                                  // [目录] 影片海报物理存放根目录
extern QString MovieVideoPath;                                                  // [目录] 影片源文件物理存放根目录
extern QString MovieRecordPath;                                                 // [文件] 观影流水 movieRecord.json 绝对路径


// ========================================================
// 5. [游戏启动器] 路径
// ========================================================
extern QString GameCoverPath;                                                   // [目录] 游戏海报物理存放根目录
extern QString GameTarPath;                                                     // [目录] 游戏压缩包分片下载与合并的临时工作区
extern QString GameInstallPath;                                                 // [目录] 游戏最终解压的物理执行根目录


// ========================================================
// 6. RAII 辅助工具类 (作用域结束自动执行)
// ========================================================
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