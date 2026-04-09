#ifndef PLAYBACKPAGE_H
#define PLAYBACKPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QDateTime>
#include <QProgressBar>
#include "common.pb.h"          
#include "server_msg.pb.h"

class PlaybackPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackPage(QWidget* parent = nullptr);                                           // 构造函数
    ~PlaybackPage();

signals:
    void playbackFinishedRecord(const QString& date, const QString& name,                       // 播放结束时抛出记录信号
        const QString& startTime, const QString& endTime,
        const QString& endType);

public slots:
    void RefreshMovies();                                                                       // 将原本的 LoadMovies 封装一下

private slots:
    void onMovieSelected(QListWidgetItem* current, QListWidgetItem* prev);                      // 卡片选中事件

    void onPlayClicked();                                                                       // 播放按钮
    void onDownloadClicked();                                                                   // 下载按钮
    void onPauseClicked();                                                                      // 暂停按钮
    void onForwardClicked();                                                                    // 快进按钮
    void onRewindClicked();                                                                     // 快退按钮
    void onStopClicked();                                                                       // 停止按钮
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);                                // 监听影片自然结束

    void onMovieListReceived(const ServerApi::GetMovieListRsp& rsp);                            // 影片数据拉取成功
    // 处理 TCPMgr 抛出的下载相关信号
    void onCoverDownloaded(const QString& fileMd5, const QString& localCoverPath);              // 海报下载完毕
    void onDownloadFailed(const QString& errMsg);                                               // 视频下载失败
    void onDownloadProgress(const QString& fileMd5, qint64 chunkSize);                          // 视频下载进度
    void onDownloadFinished(const QString& fileMd5);                                            // 视频下载完成

private:
    void BuildUI();                                                                             // 构建播放台UI
    void LoadMoviesFromJson();                                                                  // 加载视频配置文件
    void LoadMoviesFromServer();                                                                // 从服务器获取视频列表
    void AddMovieCard(uint64_t id, const QString& name, const QString& coverUrl,
        const QString& localPath, bool isDownloaded, int playStatus,
        const QString& fileMd5, uint64_t expectedSize, const QString& encryptKey);              // 创建海报card
    void SwitchControlPanelState(bool isDownloaded);                                            // 根据下载状态，整体切换底部播控面板的按钮显隐
                        
private:
    QListWidget* m_movieList;                                                                   // 影片列表
    QLabel* m_countdownLabel;                                                                   // 倒计时LCD
    QPushButton* m_btnPlay;                                                                     // 播放按钮(控制显隐)
    QPushButton* m_btnDownload;                                                                 // 下载按钮
    QPushButton* m_btnPause;                                                                    // 暂停按钮
    QPushButton* m_btnRewind;                                                                   // 快退按钮
    QPushButton* m_btnForward;                                                                  // 快进按钮
    QPushButton* m_btnStop;                                                                     // 强制结束按钮
    QProgressBar* m_downloadProgress;                                                           // 底部全局下载进度条

    QVideoWidget* m_videoWidget;                                                                // 视频投屏幕布
    QMediaPlayer* m_player;                                                                     // 底层解码引擎

    QString m_selectedMoviePath;                                                                // 选中的影片绝对路径
    QString m_selectedMovieName;                                                                // 选中的影片名称
    QDateTime m_playStartTime;                                                                  // 记录开始播放的时间

    bool m_isPlayingRecord;                                                                     // 防抖标志位，防止重复记录
    bool m_isDownloading = false;                                                               // 状态锁，防止下载时乱点
    bool     m_selectedIsDownloaded;                                                            // 当前选中的影片是否已下载
    QString  m_selectedMovieMd5;                                                                // 当前选中影片的 MD5 (用于向服务器请求分片)
    uint64_t m_selectedMovieSize;                                                               // 当前选中影片的总大小
    QString m_selectedMovieEncryptKey;                                                          // 当前选中影片的加密密钥

    qint64 m_currentDownloadBytes = 0;                                                          // 用于累加当前正在下载的视频的已接收字节数
};

#endif // PLAYBACKPAGE_H