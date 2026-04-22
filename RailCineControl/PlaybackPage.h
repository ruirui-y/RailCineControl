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

// =========================================================================================
// 卡片UI数据：用于在子线程进行耗时 IO 计算后，将纯净的数据打包跳跃回主线程进行 UI 渲染
// =========================================================================================
struct MovieUIPayload
{
    uint64_t id;                                                                                // 影片全局唯一 ID (源自云端数据库)
    QString name;                                                                               // 影片展示名称 (用于卡片 UI 渲染)
    QString localCoverPath;                                                                     // 本地海报的物理路径 (若本地缺失则在子线程中被赋为空串)
    QString localVideoPath;                                                                     // 本地视频的物理路径 (基于 MD5 纯静态推导生成)
    bool isVideoDownloaded;                                                                     // 视频是否已完整下载 (用于精准控制 [播放] 与 [下载] 按钮的互斥显示)
    int status;                                                                                 // 影片当前业务状态 (如：0=空闲, 1=播放中，影响UI状态角标)
    QString fileMd5;                                                                            // 影片核心标识 MD5 (大文件分片下载、海报请求的唯一网络凭证)
    uint64_t expectedSize;                                                                      // 影片理论总字节数 (用于在子线程与本地 QFile 做完整性校验)
    QString encryptKey;                                                                         // 影片专属加密密钥 (播放时传入播放器底层进行内存级实时解密)
    bool needFetchCover;                                                                        // 网络请求标记位：通知主线程是否需要向云端发起海报下载请求 (TCP 发包)
    QImage coverImage;                                                                          // 性能核心载体：子线程利用 QImageReader 预解码并降采样好的海报图像
};

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
        const QString& localPath, bool isDownloaded, int playStatus,const QString& fileMd5, 
        uint64_t expectedSize, const QString& encryptKey, QImage coverImage);                   // 创建海报card
    void SwitchControlPanelState(bool isDownloaded);                                            // 根据下载状态，整体切换底部播控面板的按钮显隐
                        
private:
    QListWidget* m_movieList;                                                                   // 影片列表
    QLabel* m_countdownLabel;                                                                   // 倒计时LCD
    QPushButton* m_btnFetch;                                                                    // 拉取影片
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