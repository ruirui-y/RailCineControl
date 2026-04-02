#ifndef PLAYBACKPAGE_H
#define PLAYBACKPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QDateTime>

class PlaybackPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackPage(QWidget* parent = nullptr);                       // 构造函数
    ~PlaybackPage();

signals:
    void playbackFinishedRecord(const QString& date, const QString& name,   // 播放结束时抛出记录信号
        const QString& startTime, const QString& endTime,
        const QString& endType);

private slots:
    void onMovieSelected(QListWidgetItem* current, QListWidgetItem* prev);  // 卡片选中事件

    void onPlayClicked();                                                   // 播放按钮
    void onPauseClicked();                                                  // 暂停按钮
    void onForwardClicked();                                                // 快进按钮
    void onRewindClicked();                                                 // 快退按钮
    void onStopClicked();                                                   // 停止按钮
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);            // 监听影片自然结束

private:
    void BuildUI();                                                         // 构建播放台UI
    void LoadMoviesFromJson();                                              // 加载视频配置文件
    void AddMovieCard(const QString& name, const QString& status, const QString& path); // 发牌机

private:
    QListWidget* m_movieList;                                               // 影片列表
    QLabel* m_countdownLabel;                                               // 倒计时LCD
    QPushButton* m_btnPlay;                                                 // 播放按钮(控制显隐)

    QVideoWidget* m_videoWidget;                                            // 视频投屏幕布
    QMediaPlayer* m_player;                                                 // 底层解码引擎

    QString m_selectedMoviePath;                                            // 选中的影片绝对路径
    QString m_selectedMovieName;                                            // 选中的影片名称
    QDateTime m_playStartTime;                                              // 记录开始播放的时间

    bool m_isPlayingRecord;                                                 // 防抖标志位，防止重复记录
};

#endif // PLAYBACKPAGE_H