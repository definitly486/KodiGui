#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <QMainWindow>
#include <QWebEngineView>
#include "m3u8interceptor.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerWindow; }
QT_END_NAMESPACE

class PlayerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(const QUrl &url, QWidget *parent = nullptr);
    ~PlayerWindow();

    // Добавляем этот метод, чтобы MainWindow мог вызвать извлечение
    void extractCurrentKey(); 

signals:
    void urlCaptured(const QString &url);

private:
    Ui::PlayerWindow *ui;
    bool m_mpvLaunched = false;

    void setupWidevineCapture();
    void extractKeyWithPython(const QString &pssh, const QString &licUrl, const QString &licHeaders);
    void saveKeyToFile(const QString &key, const QString &pssh, const QString &licUrl);

    QWebEngineView *webView = nullptr;
    M3u8Interceptor *m_interceptor = nullptr;
};

#endif // PLAYERWINDOW_H