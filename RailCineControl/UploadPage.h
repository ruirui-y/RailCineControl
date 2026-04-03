#ifndef UPLOADPAGE_H
#define UPLOADPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>

class UploadPage : public QWidget
{
    Q_OBJECT

public:
    explicit UploadPage(QWidget* parent = nullptr);                                 // 构造函数

signals:
    void uploadFinished();                                                          // 核心信号：上传成功后抛出，通知播放台刷新

private slots:
    void onSelectVideo();                                                           // 选择视频文件
    void onSelectCover();                                                           // 选择海报图片
    void onUploadClicked();                                                         // 点击上传按钮
    void onSimulateProgress();                                                      // 模拟进度条动画（实战中换成网络请求进度回调）

private:
    void BuildUI();                                                                 // 构建UI

    QLineEdit* m_videoPathEdit;                                                     // 视频路径显示框
    QLineEdit* m_coverPathEdit;                                                     // 封面路径显示框
    QLineEdit* m_nameEdit;                                                          // 影片名称输入框
    QTextEdit* m_descEdit;                                                          // 影片简介输入框

    QLabel* m_coverPreview;                                                         // 海报图片实时预览框
    QProgressBar* m_progressBar;                                                    // 上传进度条
    QPushButton* m_btnUpload;                                                       // 核心上传按钮

    // 模拟上传用的临时变量
    int m_currentProgress;
    class QTimer* m_mockTimer;
};

#endif // UPLOADPAGE_H