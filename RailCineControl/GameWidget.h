#ifndef GAME_WIDGET_H
#define GAME_WIDGET_H

#include <QWidget>
#include <QStackedWidget>

class GameLauncherPage;
class GameUploadPage;

class GameWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GameWidget(QWidget* parent = nullptr);

private slots:
    void onNavButtonClicked(int index);

private:
    void BuildUI();
    void BindAdminSignals();

    QStackedWidget* m_stackedWidget;
    GameLauncherPage* m_launcherPage;
    GameUploadPage* m_uploadPage;
};

#endif