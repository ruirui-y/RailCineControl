#ifndef ACCOUNT_WIDGET_H
#define ACCOUNT_WIDGET_H

#include <QWidget>

class QLabel;

class AccountWidget : public QWidget {
    Q_OBJECT
public:
    explicit AccountWidget(QWidget* parent = nullptr);
    void setUserName(const QString& name);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void BuildUI();

private slots:
    void SlotLoginOut();                                                                                // µÇ³ö

private:
    QLabel* m_name = nullptr;
    class ImageBgButton* switchBtn = nullptr;
};


#endif // !ACCOUNT_WIDGET_H