#include "Global.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include "Macro.h"
#include <QDebug>
#include <QProcess>

// ========================================================
// 1. 常量与不依赖 QApplication 的全局函数
// ========================================================
const int tipOffset = 5;

// 初始化声明的全局变量
function<void(QWidget*)> repolish = [](QWidget* Widget)
{
    Widget->style()->unpolish(Widget);
    Widget->style()->polish(Widget);
};

/* 密码加密 */
function<QString(QString)> xorString = [](QString input)
{
    QString result = input;
    int length = input.length();
    length %= 255;
    for (int i = 0; i < length; ++i)
    {
        result[i] = QChar(static_cast<ushort>(input[i].unicode() ^ static_cast<ushort>(length)));
    }
    return result;
};


// ========================================================
// 2. 全局变量定义 (分配内存，初始置空)
// ========================================================
QString ConfigPath;                                                                             // [文件] 主程序 UI 状态、基本参数的 Config.ini 绝对路径
QSettings* ConfigSettings = nullptr;                                                            // [句柄] 全局唯一的 QSettings 实例，用于全局读写 Config.ini

QString ClientConfigPath;                                                                       // [文件] 客户端核心网络、硬件节点等配置的 config.json 绝对路径
QString LoginConfigPath;                                                                        // [文件] 缓存用户账号、记住密码等凭证的 login.json 绝对路径
QString AppConfigPath;                                                                          // [文件] 存储界面语言、音量等偏好设置的 app_settings.json 绝对路径

QString TranslationsPath;                                                                       // [目录] 存放所有 .qm 国际化翻译文件的根目录

QString MovieConfigPath;                                                                        // [文件] 本地缓存的影片列表元数据 movies.json 绝对路径
QString MovieCoverPath;                                                                         // [目录] 影片海报图片 (.jpg/.png) 下载后存放的根目录
QString MovieVideoPath;                                                                         // [目录] 影片视频源文件 (.mp4/.mkv) 下载后存放的根目录
QString MovieRecordPath;                                                                        // [文件] 本地缓存的观影历史流水 movieRecord.json 绝对路径

QString GameCoverPath;                                                                          // [目录] 游戏海报图片下载后存放的根目录
QString GameTarPath;                                                                            // [目录] 游戏分片下载、组装 .tar 压缩包时所使用的临时工作目录
QString GameInstallPath;                                                                        // [目录] 游戏解压后最终的运行目录 (内部将按游戏 MD5 创建子文件夹)


// 获取语言文件路径 (此函数在运行时调用，所以可以放在这里)
QString GetLanguageFilePath(const QString& lang_code)
{
    return QDir(TranslationsPath).filePath(QString("RailCineControl_%1.qm").arg(lang_code));
}


// ========================================================
// 3. 👑 核心封装：全局路径初始化函数
// ========================================================
void InitGlobalPaths()
{
    // 获取当前工作路径和可执行文件 (.exe) 所在的绝对路径
    QString currentDir = QDir::currentPath();                                               // 当前vs所在的code工作路径
    QString appDir = QCoreApplication::applicationDirPath();

    // ========================================================
    // 🛡️ 核心守护：物理目录自动装配工具 (Lambda 闭包)
    // ========================================================
    // 1. 确保【文件夹】存在（mkpath 支持递归创建多级目录）
    auto EnsureDir = [](const QString& dirPath) {
        QDir dir;
        if (!dir.exists(dirPath)) {
            dir.mkpath(dirPath);
            qDebug() << u8"📁 [自动装配] 物理目录缺失，已自动创建:" << dirPath;
        }
    };

    // 2. 确保【文件】所在的父级目录存在（防止 QFile 写入时因找不到文件夹而崩盘）
    auto EnsureFileDir = [&EnsureDir](const QString& filePath) {
        EnsureDir(QFileInfo(filePath).absolutePath());
    };

    // --------------------------------------------------------
    // [INI 配置模块]
    // --------------------------------------------------------
    ConfigPath          = QDir(currentDir).filePath("Config/Config.ini");
    EnsureFileDir(ConfigPath); // 确保 Config 文件夹存在
    ConfigSettings      = new QSettings(ConfigPath, QSettings::IniFormat);

    // --------------------------------------------------------
    // [JSON 系统配置模块] (位于 ClientInstall/Configs)
    // --------------------------------------------------------
    ClientConfigPath    = QDir(currentDir).filePath("ClientInstall/Configs/config.json");               // 硬件与服务器寻址配置
    LoginConfigPath     = QDir(currentDir).filePath("ClientInstall/Configs/login.json");                // 登录令牌与记住密码
    AppConfigPath       = QDir(currentDir).filePath("ClientInstall/Configs/app_settings.json");         // 软件 UI 偏好设置
    
    EnsureFileDir(ClientConfigPath); // 只需要调一次，就会确保 ClientInstall/Configs 文件夹存在

    // --------------------------------------------------------
    // [多语言翻译模块]
    // --------------------------------------------------------
    TranslationsPath    = QDir(currentDir).filePath("ClientInstall/Translations");                      // 存放 .qm 文件的目录
    EnsureDir(TranslationsPath); // 这是文件夹，直接建

    // --------------------------------------------------------
    // [影片流媒体模块]
    // --------------------------------------------------------
    MovieConfigPath     = QDir(currentDir).filePath("Config/movies.json");                              // 影片库清单缓存
    MovieRecordPath     = QDir(currentDir).filePath("Config/movieRecord.json");                         // 影片播放流水日志缓存
    
    MovieCoverPath      = QDir(currentDir).filePath("Movie/Cover");                                     // 影片海报物理目录
    MovieVideoPath      = QDir(currentDir).filePath("Movie/Video");                                     // 影片实体播放文件物理目录
    
    EnsureFileDir(MovieConfigPath);
    EnsureDir(MovieCoverPath); 
    EnsureDir(MovieVideoPath);

    // --------------------------------------------------------
    // [游戏启动器模块]
    // --------------------------------------------------------
    GameCoverPath       = QDir(currentDir).filePath("Game/Cover");                                      // 游戏库海报物理目录
    GameTarPath         = QDir(currentDir).filePath("Game/Packages");                                   // 下载引擎合并分片的临时暂存区
    GameInstallPath     = QDir(currentDir).filePath("Game/Installed");                                  // UE 游戏解压拔除后的执行根目录
    
    EnsureDir(GameCoverPath);
    EnsureDir(GameTarPath);
    EnsureDir(GameInstallPath);

    qDebug() << u8"[Global] 🌍 全局环境与路径字典初始化完毕！底层文件系统已完成装配校验！引擎挂载点:" << appDir;
}