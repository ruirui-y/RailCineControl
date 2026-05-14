#ifndef GAME_UPLOAD_PAGE_H
#define GAME_UPLOAD_PAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFile>

class GameUploadPage : public QWidget
{
    Q_OBJECT

public:
    explicit GameUploadPage(QWidget* parent = nullptr);

    void ResetUI();
    void UnlockUI();

private slots:
    void onSelectFolder();
    void onSelectCover();
    void onSelectExe();                                                         // 选择程序可执行文件
    void onUploadClicked();

    // 核心管线
    void startArchiveAndUpload();
    void startTcpChunkUpload();
    void pumpNextChunk();
    void submitMetadataToTcp();

private:
    void BindSignals();
    void BuildUI();

private:
    QLineEdit* m_dirPathEdit;
    QLineEdit* m_coverPathEdit;
    QLineEdit* m_nameEdit;
    QLineEdit* m_versionEdit;
    QLineEdit* m_exePathEdit;
    QTextEdit* m_descEdit;
    QLabel* m_coverPreview;
    QProgressBar* m_progressBar;
    QPushButton* m_btnUpload;

    // 分片引擎
    QFile* m_tempTarFile;
    QString m_currentTarPath;
    QString m_currentFileMd5;
    uint64_t m_currentOffset;
    uint32_t m_chunkIndex;
    uint64_t m_totalFileSize;
};

#endif // GAME_UPLOAD_PAGE_H