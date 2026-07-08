#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QNetworkAccessManager>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void on_pushButton_info_clicked();
    void on_pushButton_2_clicked();
    void on_pushButton_3_clicked();
    void on_pushButton_4_clicked();
    void on_pushButton_5_clicked();
    void on_pushButton_6_clicked();
    void on_pushButton_7_clicked();
    void on_pushButton_8_clicked();
    void on_pushButton_9_clicked();
    void on_pushButton_10_clicked();
    void on_pushButton_11_clicked();
    void on_pushButton_12_clicked();
    void on_pushButton_13_clicked();
    void on_pushButton_14_clicked();
    void on_pushButton_15_clicked();
    void on_pushButton_16_clicked();
    void on_pushButton_17_clicked();
    void on_pushButton_18_clicked();
    void on_pushButton_clearurl_clicked();
    void on_pushButton_cleardrm_clicked();
    void on_pushButton_killstreamlink_clicked();
    void on_pushButton_rundrm_clicked();
    void on_pushButton_kill_m3u8DL_clicked();
    void on_playdrm_17_clicked();
    void on_stopdrm_18_clicked();
    void on_getonairnow_clicked();
    void on_horizontalSlider_valueChanged(int value);

private:
    Ui::MainWindow *ui;
    QProcess *pythonProcess;
    QNetworkAccessManager *manager;
    int rpcId = 1;

    QString on_lineEdit_textChanged();
    QString on_lineEdit_2_textChanged();
    QString on_lineEdit_3_textChanged();
    QString on_lineEdit_drm_textChanged();
    QString on_lineEdit_drm_key_textChanged();

    void sendJsonRpc(const QJsonObject &json, const QString &desc, std::function<void(const QJsonObject&)> onSuccess = nullptr);
    void postPlayerOpenFile(const QString &file);
    void postPlayerOpenChannel(int channelId);
    void postPlayerStop();
    void postSetVolume(int volume);
    void postInputAction(const QString &action);
    void postSetAddonEnabled(const QString &addonId, const QJsonValue &enabledValue);
    void sshKillProcess(const QString &processName);
    void clearLineEditField(class QLineEdit *edit, const QString &placeholder);
};
#endif // MAINWINDOW_H
