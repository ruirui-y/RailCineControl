#ifndef UPLOADPAGE_H
#define UPLOADPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFile>
#include <QTimer>

class UploadPage : public QWidget
{
    Q_OBJECT

public:
    explicit UploadPage(QWidget* parent = nullptr);

    void ResetUI();
    void UnlockUI();

private slots:
    void onSelectVideo();
    void onSelectCover();
    void onUploadClicked();

    // TCP 纯净版分片上传管线
    void startTcpChunkUpload();                                                     // 启动切片引擎
    void pumpNextChunk();                                                           // 定时器触发：抽下一块数据
    void submitMetadataToTcp();                                                     // 视频传完后，提交海报和描述信息

private:
    void BuildUI();

    QLineEdit* m_videoPathEdit;
    QLineEdit* m_coverPathEdit;
    QLineEdit* m_nameEdit;
    QTextEdit* m_descEdit;
    QLabel* m_coverPreview;
    QProgressBar* m_progressBar;
    QPushButton* m_btnUpload;

    // ================= 分片上传核心引擎变量 =================
    QFile* m_videoFile;                                                             // 当前正在读取的视频文件
    QTimer* m_chunkPumpTimer;                                                       // 抽水泵定时器

    QString  m_currentFileMd5;                                                      // 当前视频的唯一标识
    uint64_t m_currentOffset;                                                       // 当前已读取的字节偏移量
    uint32_t m_chunkIndex;                                                          // 当前分片序号
    uint64_t m_totalFileSize;                                                       // 文件总大小
};

#endif // UPLOADPAGE_H