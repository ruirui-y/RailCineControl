#ifndef MOVIEWIDGET_H
#define MOVIEWIDGET_H

#include <QWidget>
#include <QStackedWidget>
#include "PlaybackPage.h"
#include "RecordPage.h"
#include "UploadPage.h"

class MovieWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MovieWidget(QWidget* parent = nullptr);                        // 构造函数

private slots:
    void onNavButtonClicked(int index);                                     // 导航栏切换

private:
    void BuildUI();                                                         // 构建总框架UI

    QStackedWidget* m_stackedWidget;                                        // 堆栈容器
    PlaybackPage* m_playbackPage;                                           // 视频播控子模块
    RecordPage* m_recordPage;                                               // 数据记录子模块
    UploadPage* m_uploadPage;                                               // 影片上传模块
};

#endif // MOVIEWIDGET_H