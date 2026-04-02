#ifndef TITLEBAR_H
#define TITLEBAR_H
#include <QWidget>


class QToolButton;
class QPushButton;

class TitleBar : public QWidget 
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

signals:
    void backRequested();
    void settingsRequested();
    void minimizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    void InitBtns();

private:
    QPoint m_dragOffset;                                                            // 鼠标相对窗口左上角的偏移
    QPushButton* m_btnName = nullptr;                                               // 名称
    QToolButton* m_btnMin = nullptr;                                                // 最小化
    QToolButton* m_btnClose = nullptr;                                              // 关闭
};

#endif // TITLEBAR_H