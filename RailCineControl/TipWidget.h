#ifndef TIP_WIDGET_H
#define TIP_WIDGET_H
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>


class TipWidget  : public QWidget
{
	Q_OBJECT

public:
    static void showTip(QWidget* parent, const QString& text, int ms = 1800);

    static bool confirm(QWidget* parent,
        const QString& text,
        const QString& okText = QString("确定"),
        const QString& cancelText = QString("取消"));

private:
    explicit TipWidget(QWidget* parent);

private:
    QLabel* m_label = nullptr;
};

#endif // TIP_WIDGET_H