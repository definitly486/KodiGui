#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QString>
#include <QNetworkAccessManager>  // <-- обязательно для QNetworkAccessManager
#include <QNetworkReply>
#include <QNetworkRequest>


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
    void on_horizontalSlider_valueChanged(int value);

    QString on_lineEdit_textChanged();

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_4_clicked();

    void on_pushButton_6_clicked();

    void on_pushButton_5_clicked();

    QString  on_lineEdit_2_textChanged();

    void on_pushButton_7_clicked();

    void on_pushButton_9_clicked();

    void on_pushButton_8_clicked();

     QString  on_lineEdit_3_textChanged();
    void on_getonairnow_clicked();  // <-- добавьте эту строку

     void on_pushButton_10_clicked();

     void on_pushButton_11_clicked();

     void on_pushButton_12_clicked();

     void on_pushButton_13_clicked();

     void on_pushButton_14_clicked();

     void on_pushButton_clearurl_clicked();

     void on_pushButton_15_clicked();

     void on_pushButton_16_clicked();

     void on_playdrm_17_clicked();

     void on_stopdrm_18_clicked();

     void on_pushButton_rundrm_clicked();

      QString  on_lineEdit_drm_textChanged();

       QString  on_lineEdit_drm_key_textChanged();





       void on_pushButton_killstreamlink_clicked();

       void on_pushButton_cleardrm_clicked();

       void on_pushButton_playdrmts_clicked();

       void on_pushButton_kill_m3u8DL_clicked();

       void on_pushButton_17_clicked();

       void on_pushButton_playdrmaarts_clicked();

       void on_pushButton_18_clicked();

   private:
 Ui::MainWindow *ui;
 QProcess *pythonProcess;        // ← вот это
   QNetworkAccessManager *manager;
    int rpcId = 1;

    void sendJsonRpc(
        const QJsonObject &json,
        const QString &desc,
        std::function<void(const QJsonObject&)> onSuccess = nullptr
    );
};
#endif // MAINWINDOW_H
