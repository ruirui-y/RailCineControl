#ifndef GAME_LAUNCHER_PAGE_H
#define GAME_LAUNCHER_PAGE_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QProgressBar>
#include <QDateTime>
#include "common.pb.h"
#include "server_msg.pb.h"

// 游戏卡片异步加载数据载体
struct GameUIPayload
{
    uint64_t id;
    QString name;
    QString version;
    QString localCoverPath;
    QString localTarPath;           // 压缩包路径
    QString localInstallDir;        // 解压后的绝对目录
    QString exeRelativePath;        // 相对启动路径
    bool isDownloaded;              // 是否已解压并就绪
    QString fileMd5;                // 核心包 MD5
    uint64_t expectedSize;
    bool needFetchCover;
    QImage coverImage;
};

class GameLauncherPage : public QWidget
{
    Q_OBJECT

public:
    explicit GameLauncherPage(QWidget* parent = nullptr);
    ~GameLauncherPage();

    void RefreshGames();

private slots:
    void onGameSelected(QListWidgetItem* current, QListWidgetItem* prev);

    void onLaunchClicked();
    void onDownloadClicked();
    void onStopClicked();
    void onGameProcessFinished();

    // TCP 回调
    void onGameListReceived(const ServerApi::GetGameListRsp& rsp);
    void onCoverDownloaded(const QString& fileMd5, const QString& localCoverPath);
    void onDownloadFailed(const QString& errMsg);
    void onDownloadProgress(const QString& fileMd5, qint64 chunkSize);
    void onDownloadFinished(const QString& fileMd5);

private:
    void BuildUI();
    void AddGameCard(const GameUIPayload& data);
    void SwitchControlPanelState(bool isDownloaded);
    void ExtractGame(const QString& tarPath, const QString& destDir);

private:
    QListWidget* m_gameList;
    QPushButton* m_btnFetch;
    QPushButton* m_btnLaunch;
    QPushButton* m_btnDownload;
    QPushButton* m_btnStop;
    QProgressBar* m_downloadProgress;

    QProcess* m_gameProcess;

    // 选中状态
    bool     m_isDownloading = false;
    bool     m_selectedIsDownloaded = false;
    QString  m_selectedGameMd5;
    QString  m_selectedGameTarPath;
    QString  m_selectedGameInstallDir;
    QString  m_selectedGameExePath;
    uint64_t m_selectedGameSize;
    qint64   m_currentDownloadBytes = 0;
};

#endif // GAME_LAUNCHER_PAGE_H