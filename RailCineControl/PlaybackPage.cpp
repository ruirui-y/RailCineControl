#include "PlaybackPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDebug>
#include "Global.h"
#include "JsonTool.h"

PlaybackPage::PlaybackPage(QWidget* parent) : QWidget(parent)
{
    m_player = new QMediaPlayer(this);                                      // 实例化多媒体引擎
    m_player->setVolume(100);                                               // 默认最大音量

    m_videoWidget = new QVideoWidget();                                     // 实例化投屏幕布
    m_videoWidget->setStyleSheet("background-color: black;");
    m_videoWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_player->setVideoOutput(m_videoWidget);                                // 引擎绑定幕布

    BuildUI();                                                              // 搭建UI
    LoadMoviesFromJson();                                                   // 加载配置

    // 监听进度条
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        int s = (pos / 1000) % 60;
        int m = (pos / 60000) % 60;
        int h = (pos / 3600000) % 24;
        QTime time(h, m, s);
        m_countdownLabel->setText(time.toString("HH:mm:ss"));               // 实时更新 LCD 时间
        });
    
    // 监听播放器状态，捕捉“影片自然播放完毕”的瞬间
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &PlaybackPage::onMediaStatusChanged);
}

PlaybackPage::~PlaybackPage() {}

void PlaybackPage::BuildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ================= 1. 影片列表区 =================
    m_movieList = new QListWidget(this);
    m_movieList->setViewMode(QListView::IconMode);
    m_movieList->setResizeMode(QListView::Adjust);
    m_movieList->setSpacing(15);

    connect(m_movieList, &QListWidget::currentItemChanged,
        this, &PlaybackPage::onMovieSelected);

    // ================= 2. 底部播控区 =================
    QFrame* controlPanel = new QFrame(this);
    controlPanel->setFixedHeight(80);
    QHBoxLayout* controlLayout = new QHBoxLayout(controlPanel);

    m_countdownLabel = new QLabel("00:00:00", controlPanel);
    m_countdownLabel->setObjectName("statusLcd");

    auto createCtrlBtn = [&](const QString& text) -> QPushButton* {
        QPushButton* btn = new QPushButton(text, controlPanel);
        btn->setObjectName("controlBtn");
        btn->setFixedSize(100, 45);
        return btn;
        };

    m_btnPlay = createCtrlBtn(u8"▶ 播放");
    m_btnPlay->setEnabled(false);

    QPushButton* btnPause = createCtrlBtn(u8"⏸ 暂停/继续");
    QPushButton* btnRewind = createCtrlBtn(u8"⏪ 快退");
    QPushButton* btnForward = createCtrlBtn(u8"⏩ 快进");
    QPushButton* btnStop = createCtrlBtn(u8"⏹ 强制结束");

    controlLayout->addWidget(m_countdownLabel);
    controlLayout->addStretch();
    controlLayout->addWidget(btnRewind);
    controlLayout->addWidget(m_btnPlay);
    controlLayout->addWidget(btnPause);
    controlLayout->addWidget(btnForward);
    controlLayout->addSpacing(20);
    controlLayout->addWidget(btnStop);

    layout->addWidget(m_movieList, 1);
    layout->addWidget(controlPanel);

    connect(m_btnPlay, &QPushButton::clicked, this, &PlaybackPage::onPlayClicked);
    connect(btnPause, &QPushButton::clicked, this, &PlaybackPage::onPauseClicked);
    connect(btnStop, &QPushButton::clicked, this, &PlaybackPage::onStopClicked);
    connect(btnForward, &QPushButton::clicked, this, &PlaybackPage::onForwardClicked);
    connect(btnRewind, &QPushButton::clicked, this, &PlaybackPage::onRewindClicked);
}

void PlaybackPage::LoadMoviesFromJson()
{
    QString configPath = MovieConfigPath;                                   // 依赖 Global.h 的宏
    QJsonDocument doc;
    QString errMsg;

    if (JsonTool::Instance()->readJsonFile(configPath, doc, &errMsg)) {
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                QString name = obj["name"].toString();
                QString status = obj["status"].toString();
                QString path = obj["path"].toString();
                AddMovieCard(name, status, path);                           // 动态生成卡片
            }
        }
    }
    else {
        qDebug() << u8"加载影片配置失败：" << errMsg;
    }
}

void PlaybackPage::LoadMoviesFromServer()
{

}

void PlaybackPage::RefreshMovies()
{
    m_movieList->clear();                                                       // 刷新前先清空旧的卡片
    LoadMoviesFromServer();                                                       // 重新走一遍拉取和渲染流程
}

void PlaybackPage::AddMovieCard(const QString& name, const QString& status, const QString& path)
{
    QFrame* card = new QFrame();
    card->setObjectName("gameCard");
    card->setProperty("selected", false);

    QVBoxLayout* layout = new QVBoxLayout(card);
    QLabel* cover = new QLabel(card);
    cover->setObjectName("cardCover");
    cover->setFixedSize(160, 220);

    QLabel* title = new QLabel(name, card);
    title->setObjectName("cardTitle");

    QLabel* subTitle = new QLabel(status, card);
    subTitle->setObjectName("cardSub");

    layout->addWidget(cover);
    layout->addWidget(title);
    layout->addWidget(subTitle);

    QListWidgetItem* item = new QListWidgetItem(m_movieList);
    item->setSizeHint(QSize(180, 300));

    item->setData(Qt::UserRole, name);                                      // 隐藏变量1：影片名
    item->setData(Qt::UserRole + 1, path);                                  // 隐藏变量2：绝对路径

    m_movieList->addItem(item);
    m_movieList->setItemWidget(item, card);
}

void PlaybackPage::onMovieSelected(QListWidgetItem* current, QListWidgetItem* previous)
{
    if (previous) {
        QWidget* preWidget = m_movieList->itemWidget(previous);
        preWidget->setProperty("selected", false);
        preWidget->style()->unpolish(preWidget);
        preWidget->style()->polish(preWidget);
    }

    if (current) {
        QWidget* curWidget = m_movieList->itemWidget(current);
        curWidget->setProperty("selected", true);
        curWidget->style()->unpolish(curWidget);
        curWidget->style()->polish(curWidget);

        m_selectedMovieName = current->data(Qt::UserRole).toString();       // 提取影片名
        m_selectedMoviePath = current->data(Qt::UserRole + 1).toString();   // 提取影片路径

        m_btnPlay->setEnabled(true);                                        // 解锁播放按钮
    }
}

void PlaybackPage::onPlayClicked()
{
    if (m_selectedMoviePath.isEmpty()) return;

    if (m_player->state() == QMediaPlayer::PausedState) {
        m_player->play();                                                   // 恢复播放
        return;
    }

    m_playStartTime = QDateTime::currentDateTime();                         // 记录播放开始的真实时间
    m_isPlayingRecord = true;

    m_player->setMedia(QUrl::fromLocalFile(QFileInfo(m_selectedMoviePath).absoluteFilePath()));

    QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.count() > 1) {
        QRect screenGeometry = screens.at(1)->geometry();                   // 获取副屏坐标
        m_videoWidget->setGeometry(screenGeometry);                         // 直接覆盖副屏坐标
    }
    else {
        m_videoWidget->setGeometry(screens.at(0)->geometry());              // 单屏保底
    }

    m_videoWidget->show();
    m_player->play();
}

void PlaybackPage::onPauseClicked()
{
    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_player->pause();
    }
    else if (m_player->state() == QMediaPlayer::PausedState) {
        m_player->play();
    }
}

void PlaybackPage::onForwardClicked()
{
    if (m_player) {
        qint64 currentPos = m_player->position();
        qint64 duration = m_player->duration();
        qint64 targetPos = currentPos + 15000;                              // 快进 15 秒

        if (targetPos > duration) targetPos = duration;                     // 防越界
        m_player->setPosition(targetPos);
    }
}

void PlaybackPage::onRewindClicked()
{
    if (m_player) {
        qint64 currentPos = m_player->position();
        qint64 targetPos = currentPos - 15000;                              // 快退 15 秒

        if (targetPos < 0) targetPos = 0;                                   // 防负数
        m_player->setPosition(targetPos);
    }
}

// 强制结束逻辑
void PlaybackPage::onStopClicked()
{
    if (m_player) m_player->stop();
    if (m_countdownLabel) m_countdownLabel->setText("00:00:00");
    if (m_videoWidget && m_videoWidget->isVisible()) m_videoWidget->close();

    // 只有在有效播放状态下，才抛出记录信号
    if (m_isPlayingRecord) {
        QDateTime endTime = QDateTime::currentDateTime();
        QString dateStr = m_playStartTime.toString("yyyy-MM-dd");
        QString startStr = m_playStartTime.toString("HH:mm:ss");
        QString endStr = endTime.toString("HH:mm:ss");

        emit playbackFinishedRecord(dateStr, m_selectedMovieName, startStr, endStr, u8"强制结束");
        qDebug() << "333333333333";
        m_isPlayingRecord = false;                                          // 💡 结算完成，标志位复位
    }
}

// 自然结束逻辑
void PlaybackPage::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    // 如果播放器报告：媒体已到达末尾 (EndOfMedia)
    if (status == QMediaPlayer::EndOfMedia) 
    {
        if (m_countdownLabel) m_countdownLabel->setText("00:00:00");
        if (m_videoWidget && m_videoWidget->isVisible()) m_videoWidget->close();

        if (m_isPlayingRecord) 
        {
            QDateTime endTime = QDateTime::currentDateTime();
            QString dateStr = m_playStartTime.toString("yyyy-MM-dd");
            QString startStr = m_playStartTime.toString("HH:mm:ss");
            QString endStr = endTime.toString("HH:mm:ss");

            // 👑 抛出带有 "正常结束" 标签的记录
            emit playbackFinishedRecord(dateStr, m_selectedMovieName, startStr, endStr, u8"正常结束");
            m_isPlayingRecord = false;                                      // 💡 结算完成，标志位复位
        }
    }
}