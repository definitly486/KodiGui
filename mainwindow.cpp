#include "mainwindow.h"
#include "ui_kodigui.h"
#include "mainwindow.h"
#include <QDebug>
#include <QProcess>
#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}


void postkodi(int value) {



    QNetworkAccessManager *mgr = new QNetworkAccessManager();

    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));

    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");


    QJsonObject obj;

    obj["jsonrpc"] = "2.0";

    obj["id"] = "1";

    obj["method"] = "Application.SetVolume";

    obj["params"] = QJsonObject({{"volume", value}});

    QJsonDocument doc(obj);

    QByteArray data = doc.toJson();

    // or

    // QByteArray data("{\"key1\":\"value1\",\"key2\":\"value2\"}");

    //curl -X POST -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"Application.SetVolume","params":{"volume":80}}' http://192.168.8.45:8081/jsonrpc

    //QByteArray data("{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"method\":\"Application.SetVolume\",\"params\":\{\"volume\":  50}}");

    QNetworkReply *reply = mgr->post(request, data);


    QObject::connect(reply, &QNetworkReply::finished, [=](){

        if(reply->error() == QNetworkReply::NoError){

            QString contents = QString::fromUtf8(reply->readAll());

            qDebug() << contents;

        }

        else{

            QString err = reply->errorString();

            qDebug() << err;

        }

        reply->deleteLater();

    });



}


void MainWindow::on_horizontalSlider_valueChanged(int value)

{

    postkodi(value);

    qDebug() << "Значение горизонтального слайдера изменилось:" << value;


}




void MainWindow::on_pushButton_clicked()

{

    QString input = on_lineEdit_textChanged();

    qDebug()<< input;

    QProcess process;

    QStringList arguments;

    arguments << input;

    QStringList anotherList = {input};

    QString program = "kodidlp";

    process.setProgram(program);

    process.setArguments(anotherList);

    process.start();

    process.waitForFinished();

}


QString  MainWindow::on_lineEdit_textChanged()

{
     QString input = ui->lineEdit->text();
     return input;
}

void MainWindow::on_pushButton_2_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");


    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = "1";
    obj["method"] = "Player.Open";

    QJsonObject params;
    params["item"] = QJsonObject({{"channelid", 1}});
    obj["params"] = params;
    
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

    //curl -X POST -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"Player.Open","params":{"item":{"channelid":1}}}' http://192.168.8.45:8081/jsonrpc

    QNetworkReply *reply = mgr->post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=](){

        if(reply->error() == QNetworkReply::NoError){
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << contents;

        }

        else{
        QString err = reply->errorString();
        qDebug() << err;
         }

       reply->deleteLater();

    });
}


void MainWindow::on_pushButton_3_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");


    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = "Player.Stop";

    QJsonObject params;
    params["playerid"] =1;
    obj["params"] = params;
    obj["id"] = "1";
    
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

  //curl -X POST -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "Player.Stop", "params": { "playerid": 1 }, "id": 1}'  http://192.168.8.45:8081/jsonrpc

    QNetworkReply *reply = mgr->post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=](){

        if(reply->error() == QNetworkReply::NoError){
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << contents;

        }

        else{
        QString err = reply->errorString();
        qDebug() << err;
         }

       reply->deleteLater();

    });

}


void MainWindow::on_pushButton_4_clicked()
{


    QProcess process;
    QStringList arguments;
    arguments   << "-p"  	
		<< "639639"            
		<< "ssh"
                <<  "pi@192.168.8.45"
                <<  "kodi &";                     
    process.start("sshpass", arguments);
    process.waitForFinished();        

}


void MainWindow::on_pushButton_6_clicked()
{

    QProcess process;
    QStringList arguments;
    arguments   << "-p"
              << "639639"
              << "ssh"
              <<  "pi@192.168.8.45"
              <<  "killall -9 kodi.bin  &";
    process.start("sshpass", arguments);
    process.waitForFinished();

}


void MainWindow::on_pushButton_5_clicked()
{
    QProcess process;
    QStringList arguments;
    arguments   << "-p"
              << "639639"
              << "ssh"
              <<  "pi@192.168.8.45"
              <<  "sudo reboot &";
    process.start("sshpass", arguments);
    process.waitForFinished();
}




QString  MainWindow::on_lineEdit_2_textChanged()
{
    QString input = ui->lineEdit_2->text();
    return input;
}


void MainWindow::on_pushButton_7_clicked()
{
    QString input = on_lineEdit_2_textChanged();

    qDebug()<< input;

    QProcess process;

    QStringList arguments;

    arguments << input;

    QStringList anotherList = {input};

    QString program = "echoplaylist";

    process.setProgram(program);

    process.setArguments(anotherList);

    process.start();

    process.waitForFinished();

}


void MainWindow::on_pushButton_9_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");


    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = 1;
    obj["method"] = "Player.Open";




    QJsonObject params;
    QJsonObject item;
    item["file"] = "yt.mp4";

    params["item"] = item;
    obj["params"] = params;

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

    //curl -X POST -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"Player.Open","params":{"item":{"file":"yt.mp4"}}}'  http://192.168.8.45:8081/jsonrpc

    QNetworkReply *reply = mgr->post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=](){

        if(reply->error() == QNetworkReply::NoError){
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << contents;

        }

        else{
            QString err = reply->errorString();
            qDebug() << err;
        }

        reply->deleteLater();

    });


}


void runCommand(const QString &command, const QStringList &args = {}) {
    QProcess process;
    process.start(command, args);
    process.waitForFinished(-1);
    qDebug() << "Команда:" << command << args;
    qDebug() << "Вывод:" << process.readAllStandardOutput();
    qDebug() << "Ошибки:" << process.readAllStandardError();
}


void MainWindow::on_pushButton_8_clicked()
{
    QString sshPrefix = "ssh";
    QString user = "pi@192.168.8.45";

    // Последовательное выполнение команд с задержками
    auto executeSequence = [&]() {
        // 1. sudo systemctl start youtubeUnblock
        runCommand(sshPrefix, {user, "sudo systemctl start youtubeUnblock"});

        // 2. rm yt.mp4
        runCommand(sshPrefix, {user, "rm yt.mp4"});

        // 3. killall -9 yt-dlp
        runCommand(sshPrefix, {user, "killall -9 yt-dlp"});

        // 4. killall -9 ffmpeg
        runCommand(sshPrefix, {user, "killall -9 ffmpeg"});

        // 5. sudo systemctl restart youtubeUnblock
        runCommand(sshPrefix, {user, "sudo systemctl restart youtubeUnblock"});

        // 6. ./yt.sh $URL &
        QString url = "your_video_url"; // замените на ваш URL
        QString input = on_lineEdit_3_textChanged();
       runCommand(sshPrefix, {user, "nohup ./yt.sh " + input + " > /dev/null 2>&1 &"});

        // 7. sleep 40
        QTimer::singleShot(40000, []() {
            qDebug() << "Прошло 40 секунд.";
            // Можно добавить дальнейшие действия после ожидания
        });
    };

    // Запуск последовательности
    executeSequence();

}


QString MainWindow::on_lineEdit_3_textChanged()
{
    QString input = ui->lineEdit_3->text();
    return input;
}


void MainWindow::on_pushButton_10_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");


    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = "Player.Stop";

    QJsonObject params;
    params["playerid"] =1;
    obj["params"] = params;
    obj["id"] = "1";

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

    //curl -X POST -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "Player.Stop", "params": { "playerid": 1 }, "id": 1}'  http://192.168.8.45:8081/jsonrpc

    QNetworkReply *reply = mgr->post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=](){

        if(reply->error() == QNetworkReply::NoError){
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << contents;

        }

        else{
            QString err = reply->errorString();
            qDebug() << err;
        }

        reply->deleteLater();

    });

}

