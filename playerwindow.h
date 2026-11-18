#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <QMainWindow>
#include <QWebEngineView>

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerWindow; }
QT_END_NAMESPACE

class PlayerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(const QUrl &url, QWidget *parent = nullptr);
    ~PlayerWindow();

signals:
    void urlCaptured(const QString &url); // Сигнал для передачи URL обратно в MainWindow


private:
    Ui::PlayerWindow *ui;
    QWebEngineView *webView = nullptr;

    bool m_mpvLaunched = false;   // защита от повторного запуска mpv
};

#endif // PLAYERWINDOW_H