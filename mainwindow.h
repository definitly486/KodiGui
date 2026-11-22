#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QNetworkAccessManager>  // <-- обязательно для QNetworkAccessManager
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QEvent>


class QWebEngineProfile;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_volumeSlider_valueChanged(int value);

    QString currentKodidlpUrl();

    void on_showStreamlinkInfoButton_clicked();

    void on_startKodidlpButton_clicked();

    void on_playPvrChannelButton_clicked();

    void on_stopPvrChannelButton_clicked();

    void on_startKodiButton_clicked();

    void on_stopKodiButton_clicked();

    void on_rebootKodiButton_clicked();

    QString  currentIptvUrl();

    void on_playIptvUrlButton_clicked();

    void on_playYoutubeFileButton_clicked();

    void on_startYoutubeDownloadButton_clicked();

    void on_startYoutubeStreamlinkButton_clicked();
    void on_killYoutubeStreamlinkButton_clicked();
    void on_closeYoutubePopupButton_clicked();

     QString  currentYoutubeUrl();
    void on_getonairnow_clicked();  // <-- добавьте эту строку

     void on_stopYoutubePlaybackButton_clicked();

     void on_stopYtDlpButton_clicked();

     void on_stopFfmpegButton_clicked();

     void on_toggleIptvSimpleClientButton_clicked();

     void on_sendBackActionButton_clicked();

     void on_clearUrlFieldsButton_clicked();

     void on_captureMatchTvUrlButton_clicked();

     void on_playMatchTvIptvButton_clicked();

     void on_startMatchTvStreamlinkButton_clicked();
     void on_playMatchTvFileButton_clicked();

     void on_playDrmStreamButton_clicked();

     void on_stopDrmPlaybackButton_clicked();

     void on_startDrmDownloadButton_clicked();
     void on_startDrmLegacyDownloadButton_clicked();

      QString  currentDrmUrl();

       QString  currentDrmKey();





       void on_stopStreamlinkButton_clicked();
       void on_killMatchTvStreamlinkButton_clicked();
       void on_closeMatchTvPopupButton_clicked();

       void on_clearDrmFieldsButton_clicked();

       void on_playDrmHlsButton_clicked();

       void on_stopDrmDownloadButton_clicked();

       void on_closePopupButton_clicked();


       void on_refreshSystemMonitorButton_clicked();

    void on_debugClearButton_clicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

   private:
 Ui::MainWindow *ui;
 QWebEngineProfile *okkoProfile = nullptr;
 QProcess *pythonProcess;        // ← вот это
 QProcess *kodidlpProcess;
 QTimer *drmFileCheckTimer = nullptr;
 QProcess *drmFileCheckProcess = nullptr;
 int drmFileCheckAttempts = 0;
 bool drmFileCheckRunning = false;
 QTimer *drmLegacyFileCheckTimer = nullptr;
 QProcess *drmLegacyFileCheckProcess = nullptr;
 int drmLegacyFileCheckAttempts = 0;
 bool drmLegacyFileCheckRunning = false;
   QNetworkAccessManager *manager;
   QPoint mousePadPressPos;
   bool mousePadPressed = false;
    int rpcId = 1;

    void sendJsonRpc(
        const QJsonObject &json,
        const QString &desc,
        std::function<void(const QJsonObject&)> onSuccess = nullptr
    );

    // --- Общие хелперы для Kodi JSON-RPC (убирают дублирование boilerplate'а) ---
    void postPlayerOpenFile(const QString &file);
    void postPlayerOpenChannel(int channelId);
    void postPlayerStop();
    void postSetVolume(int volume);
    void postInputAction(const QString &action);
    void postSetAddonEnabled(const QString &addonId, const QJsonValue &enabledValue);

    // --- Общий хелпер для SSH-команд (killall на Raspberry Pi) ---
    void sshKillProcess(const QString &processName);

    // --- Общий хелпер для очистки полей ввода ---
    void clearLineEditField(class QLineEdit *edit, const QString &placeholder);
};
#endif // MAINWINDOW_H
