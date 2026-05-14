#include "Global.h"
#include <QDir>
#include <QCoreApplication>
#include "Macro.h"
#include <QDebug>
#include <QProcess>


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

// 配置文件路径
QString ConfigPath = QDir::currentPath() + "/Config/Config.ini";

// 管理配置文件对象
QSettings* ConfigSettings = new QSettings(ConfigPath, QSettings::IniFormat);

// 客户端配置文件路径
QString ClientConfigPath = QDir(QCoreApplication::applicationDirPath()).filePath("ClientInstall/Configs/config.json");

// 登录配置文件路径
QString LoginConfigPath = QDir(QCoreApplication::applicationDirPath()).filePath("ClientInstall/Configs/login.json");

// 与 login.json 放在同级目录
QString AppConfigPath = QDir(QCoreApplication::applicationDirPath()).filePath("ClientInstall/Configs/app_settings.json");

// 翻译文件配置路径
QString TranslationsPath = QDir(QCoreApplication::applicationDirPath()).filePath("ClientInstall/Translations");
QString GetLanguageFilePath(const QString& lang_code)
{
	return QDir(TranslationsPath).filePath(QString("RailCineControl_%1.qm").arg(lang_code));
}

// 影片配置路径
QString MovieConfigPath = QDir(QCoreApplication::applicationDirPath()).filePath("Config/movies.json");								// 影片配置文件路径
QString MovieCoverPath = QDir(QCoreApplication::applicationDirPath()).filePath("Movie/Cover");										// 影片海报路径
QString MovieVideoPath = QDir(QCoreApplication::applicationDirPath()).filePath("Movie/Video");										// 影片视频路径
QString MovieRecordPath = QDir(QCoreApplication::applicationDirPath()).filePath("Config/movieRecord.json");							// 影片播放记录配置文件路径


// 游戏配置文件路径
QString GameCoverPath = QDir(QCoreApplication::applicationDirPath()).filePath("Game/Cover");										// 游戏海报缓存路径
QString GameTarPath = QDir(QCoreApplication::applicationDirPath()).filePath("Game/Packages");										// 游戏下载的压缩包临时路径
QString GameInstallPath = QDir(QCoreApplication::applicationDirPath()).filePath("Game/Installed");									// 游戏解压后的最终运行目录

const int tipOffset = 5;