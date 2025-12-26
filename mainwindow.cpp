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
#include "playerwindow.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFile>
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QTimer>
#include <QJsonArray>
#include <functional>
#include <QtConcurrent>
#include <QFuture>
#include <QPointer>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QTimer>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pythonProcess(new QProcess(this))        // ← обязательно this!
    , manager(new QNetworkAccessManager(this))  // <-- создаём здесь!
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
if (pythonProcess->state() != QProcess::NotRunning) {
        pythonProcess->terminate();
        if (!pythonProcess->waitForFinished(1000)) {
            pythonProcess->kill();                 // если не хочет по-хорошему
        }
    }
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

void MainWindow::sendJsonRpc(
    const QJsonObject &json,
    const QString &desc,
    std::function<void(const QJsonObject&)> onSuccess)
{
    QNetworkRequest request(QUrl("http://192.168.8.45:8081/jsonrpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = manager->post(
        request,
        QJsonDocument(json).toJson()
        );

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << desc << "failed:" << reply->errorString();
            reply->deleteLater();
            return;
        }

        QJsonObject response =
            QJsonDocument::fromJson(reply->readAll()).object();

        qDebug() << desc << "OK";

        if (onSuccess)
            onSuccess(response);

        reply->deleteLater();
    });
}

void MainWindow::on_pushButton_clicked()
{
    // Берём текст из lineEdit
    QString input = ui->lineEdit->text();
    QString filePath = "/tmp/list.m3u";

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file)
        << "#EXTM3U\n"
        << "#EXTINF:-1,My Channel Name\n"
        << input << "\n";
    file.close();
    qDebug() << "Temporary M3U created at" << filePath;

    // 2️⃣ Останавливаем плеер
    QJsonObject stop;
    stop["jsonrpc"] = "2.0";
    stop["method"] = "Player.Stop";
    stop["params"] = QJsonObject{{"playerid", 1}};
    stop["id"] = rpcId++;
    sendJsonRpc(stop, "Player.Stop", [=](const QJsonObject &)
    {
        // 3️⃣ Копируем M3U на веб-сервер через SCP
        QProcess *scp = new QProcess(this);
        connect(scp, &QProcess::finished, this, [=]()
        {
            // 4️⃣ Включаем PVR аддон
            QJsonObject enable;
            enable["jsonrpc"] = "2.0";
            enable["method"] = "Addons.SetAddonEnabled";
            enable["params"] = QJsonObject{{"addonid","pvr.iptvsimple"}, {"enabled", true}};
            enable["id"] = rpcId++;
            sendJsonRpc(enable, "Enable PVR", [=](const QJsonObject &)
            {
                // 5️⃣ Запускаем PVR scan
                QJsonObject scan;
                scan["jsonrpc"] = "2.0";
                scan["method"] = "PVR.Scan";
                scan["id"] = rpcId++;
                sendJsonRpc(scan, "PVR scan");

                // 6️⃣ Ждём появления каналов с лимитом 3 попытки
                auto waitForChannels = std::make_shared<std::function<void(int)>>();

                *waitForChannels = [=](int attemptsLeft)
                {
                    QJsonObject getGroups;
                    getGroups["jsonrpc"] = "2.0";
                    getGroups["method"] = "PVR.GetChannelGroups";
                    getGroups["id"] = rpcId++;

                    sendJsonRpc(getGroups, "Get Channel Groups", [=](const QJsonObject &resp)
                    {
                        QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();

                        if(!groups.isEmpty())
                        {
                            QString channelGroupId = groups.first().toObject()["channelgroupid"].toString();

                            QJsonObject getChannels;
                            getChannels["jsonrpc"] = "2.0";
                            getChannels["method"] = "PVR.GetChannels";
                            getChannels["params"] = QJsonObject{
                                {"channelgroupid", channelGroupId},
                                {"properties", QJsonArray{"channelnumber","label"}}
                            };
                            getChannels["id"] = rpcId++;

                            sendJsonRpc(getChannels, "Get Channels", [=](const QJsonObject &resp)
                            {
                                QJsonArray channels = resp["result"].toObject()["channels"].toArray();
                                if(channels.isEmpty() && attemptsLeft > 1)
                                {
                                    qDebug() << "No channels yet, retrying...";
                                    QTimer::singleShot(1000, this, [waitForChannels, attemptsLeft]()
                                    {
                                        (*waitForChannels)(attemptsLeft - 1);
                                    });
                                    return;
                                }

                                // 🔹 Открываем первый канал (если пусто, используем M3U напрямую)
                                int channelId = 1; // дефолт для Player.Open
                                if(!channels.isEmpty()) {
                                    channelId = channels.first().toObject()["channelid"].toInt();
                                    qDebug() << "Opening first PVR channel:" << channels.first().toObject()["label"].toString();
                                } else {
                                    qDebug() << "No PVR channels found, opening default first channel from M3U";
                                }

                                QJsonObject play;
                                play["jsonrpc"] = "2.0";
                                play["method"] = "Player.Open";
                                play["params"] = QJsonObject{{"item", QJsonObject{{"channelid", channelId}}}};
                                play["id"] = rpcId++;
                                sendJsonRpc(play, "Player.Open");
                            });
                        }
                        else
                        {
                            if(attemptsLeft > 1)
                            {
                                qDebug() << "No channel groups yet, retrying...";
                                QTimer::singleShot(1000, this, [waitForChannels, attemptsLeft]()
                                {
                                    (*waitForChannels)(attemptsLeft - 1);
                                });
                                return;
                            }

                            // После 3 попыток — открываем первый канал из M3U
                            qDebug() << "No channel groups after 3 attempts, opening default first channel from M3U";
                            QJsonObject play;
                            play["jsonrpc"] = "2.0";
                            play["method"] = "Player.Open";
                            play["params"] = QJsonObject{{"item", QJsonObject{{"channelid", 1}}}};
                            play["id"] = rpcId++;
                            sendJsonRpc(play, "Player.Open");
                        }
                    });
                };

                // Запуск ожидания каналов: 3 попытки
                (*waitForChannels)(3);
            }); // end sendJsonRpc Enable PVR
        }); // end connect scp finished

        scp->start("sshpass", {"-p", "639639", "scp", filePath, "pi@192.168.8.45:/var/www/html"});
    }); // end sendJsonRpc Player.Stop
} // end on_playurl_clicked


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
    // Берём текст из lineEdit_2
    QString input = ui->lineEdit_2->text();
    QString filePath = "/tmp/list.m3u";

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file) << "#EXTM3U\n" << "#EXTINF:-1,My Channel Name\n" << input << "\n";
    file.close();
    qDebug() << "Temporary M3U created at" << filePath;

    QPointer<MainWindow> safeThis(this);

    // 2️⃣ Останавливаем плеер
    QJsonObject stop;
    stop["jsonrpc"] = "2.0";
    stop["method"] = "Player.Stop";
    stop["params"] = QJsonObject{{"playerid", 1}};
    stop["id"] = rpcId++;

    sendJsonRpc(stop, "Player.Stop", [safeThis, filePath](const QJsonObject &){
        if(!safeThis) return;

        // 3️⃣ Асинхронный SFTP upload
        QtConcurrent::run([safeThis, filePath]() {
            const QString host = "192.168.8.45";
            const int port = 22;
            const QString user = "pi";
            const QString password = "639639";
            const QString remoteFile = "/var/www/html/list.m3u";

            // TCP socket
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { qDebug() << "Socket error"; return; }

            struct sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
            struct hostent* he = gethostbyname(host.toUtf8().constData());
            if (!he) { ::close(sock); return; }
            sin.sin_addr = *(struct in_addr*)he->h_addr;

            if (::connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) { ::close(sock); return; }

            // libssh2 session
            LIBSSH2_SESSION* session = libssh2_session_init();
            if (!session) { ::close(sock); return; }
            if (libssh2_session_handshake(session, sock)) { libssh2_session_free(session); ::close(sock); return; }
            if (libssh2_userauth_password(session, user.toUtf8().constData(), password.toUtf8().constData())) {
                libssh2_session_disconnect(session, "Bye");
                libssh2_session_free(session);
                ::close(sock);
                return;
            }

            // SFTP
            LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
            if (!sftp) { libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }

            LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(sftp,
                remoteFile.toUtf8().constData(),
                LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);

            if (!sftpHandle) { libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }

            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) { libssh2_sftp_close(sftpHandle); libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }
            QByteArray data = file.readAll();
            libssh2_sftp_write(sftpHandle, data.constData(), data.size());

            libssh2_sftp_close(sftpHandle);
            libssh2_sftp_shutdown(sftp);
            libssh2_session_disconnect(session, "Bye");
            libssh2_session_free(session);
            ::close(sock);

            qDebug() << "File uploaded successfully:" << remoteFile;

            // 4️⃣ JSON-RPC в основном потоке
            if (!safeThis) return;
            QMetaObject::invokeMethod(safeThis, [safeThis]() {

                // Включаем PVR
                QJsonObject enable;
                enable["jsonrpc"] = "2.0";
                enable["method"] = "Addons.SetAddonEnabled";
                enable["params"] = QJsonObject{{"addonid","pvr.iptvsimple"}, {"enabled", true}};
                enable["id"] = safeThis->rpcId++;

                safeThis->sendJsonRpc(enable, "Enable PVR", [safeThis](const QJsonObject &){
                    if(!safeThis) return;

                    // Запуск PVR scan
                    QJsonObject scan;
                    scan["jsonrpc"]="2.0";
                    scan["method"]="PVR.Scan";
                    scan["id"]=safeThis->rpcId++;
                    safeThis->sendJsonRpc(scan,"PVR scan");

                    // Ждём появления каналов
                    auto waitForChannels = std::make_shared<std::function<void(int)>>();
                    *waitForChannels = [safeThis, waitForChannels](int attemptsLeft){
                        if(!safeThis) return;
                        QJsonObject getGroups;
                        getGroups["jsonrpc"]="2.0";
                        getGroups["method"]="PVR.GetChannelGroups";
                        getGroups["id"]=safeThis->rpcId++;

                        safeThis->sendJsonRpc(getGroups,"Get Channel Groups",[safeThis, attemptsLeft, waitForChannels](const QJsonObject &resp){
                            if(!safeThis) return;
                            QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                            if(!groups.isEmpty()) {
                                QString groupId = groups.first().toObject()["channelgroupid"].toString();

                                QJsonObject getChannels;
                                getChannels["jsonrpc"]="2.0";
                                getChannels["method"]="PVR.GetChannels";
                                getChannels["params"]=QJsonObject{
                                    {"channelgroupid", groupId},
                                    {"properties", QJsonArray{"channelnumber","label"}}
                                };
                                getChannels["id"]=safeThis->rpcId++;

                                safeThis->sendJsonRpc(getChannels,"Get Channels",[safeThis](const QJsonObject &resp){
                                    if(!safeThis) return;
                                    QJsonArray channels = resp["result"].toObject()["channels"].toArray();
                                    int channelId = 1;
                                    if(!channels.isEmpty()){
                                        channelId = channels.first().toObject()["channelid"].toInt();
                                        qDebug()<<"Opening first PVR channel:"<<channels.first().toObject()["label"].toString();
                                    }
                                    QJsonObject play;
                                    play["jsonrpc"]="2.0";
                                    play["method"]="Player.Open";
                                    play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",channelId}}}};
                                    play["id"]=safeThis->rpcId++;
                                    safeThis->sendJsonRpc(play,"Player.Open");
                                });
                            } else if(attemptsLeft>1){
                                QTimer::singleShot(1000,safeThis,[waitForChannels,attemptsLeft](){(*waitForChannels)(attemptsLeft-1);});
                            } else {
                                QJsonObject play;
                                play["jsonrpc"]="2.0";
                                play["method"]="Player.Open";
                                play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",1}}}};
                                play["id"]=safeThis->rpcId++;
                                safeThis->sendJsonRpc(play,"Player.Open");
                            }
                        });
                    };
                    (*waitForChannels)(3);
                });

            }, Qt::QueuedConnection);

        });

    });
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


void runYTmp4(){


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
       runCommand(sshPrefix, {user, "$HOME/.local/bin/yt-dlp  -f 91 " + input + " --no-part   -o yt.mp4 " " > /dev/null 2>&1 &"});
        // 7. sleep 50
        QTimer::singleShot(50000, []() {
            qDebug() << "Прошло 50 секунд.";
            // Можно добавить дальнейшие действия после ожидания
             runYTmp4();

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


void MainWindow::on_pushButton_11_clicked()
{

    QString sshPrefix = "ssh";
    QString user = "pi@192.168.8.45";

    // Последовательное выполнение команд с задержками
    auto executeSequence = [&]() {

        // 3. killall -9 yt-dlp
        runCommand(sshPrefix, {user, "killall -9 yt-dlp"});



    };

    // Запуск последовательности
    executeSequence();

}


void MainWindow::on_pushButton_12_clicked()
{
    QString sshPrefix = "ssh";
    QString user = "pi@192.168.8.45";

    // Последовательное выполнение команд с задержками
    auto executeSequence = [&]() {

        // 3. killall -9 yt-dlp
        runCommand(sshPrefix, {user, "killall -9 ffmpeg"});



    };

    // Запуск последовательности
    executeSequence();

}


void MainWindow::on_pushButton_13_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();

    // Define the URL
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));

    // Setup the request
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Build the JSON object for toggling the addon
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = 1;
    obj["method"] = "Addons.SetAddonEnabled";

    QJsonObject params;
    params["addonid"] = "pvr.iptvsimple";
    params["enabled"] = "toggle"; // or true/false as per API requirements

    obj["params"] = params;

    //JSON='{"jsonrpc":"2.0","method":"Addons.SetAddonEnabled","params":{"addonid":"pvr.iptvsimple","enabled":"toggle"},"id":1}'

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

    // Send POST request
    QNetworkReply *reply = mgr->post(request, data);

    // Handle reply
    QObject::connect(reply, &QNetworkReply::finished, [=](){
        if(reply->error() == QNetworkReply::NoError){
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << "Response:" << contents;
        }
        else{
            QString err = reply->errorString();
            qDebug() << "Error:" << err;
        }
        reply->deleteLater();
    });


}


void MainWindow::on_pushButton_14_clicked()
{
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);

    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = "Input.ExecuteAction";
    QJsonObject paramsObj;
    paramsObj["action"] = "back";
    obj["params"] = paramsObj;
    obj["id"] = 1;

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();

    QNetworkReply *reply = mgr->post(request, data);

    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << "Response:" << contents;
        } else {
            QString err = reply->errorString();
            qDebug() << "Error:" << err;
        }
        reply->deleteLater();
    });
}


void MainWindow::on_pushButton_clearurl_clicked()
{
    ui->lineEdit_2->clear();                    // вот твоя очистка URL
    ui->lineEdit_2->setPlaceholderText("Введите URL...");
    ui->lineEdit_2->setFocus();

    ui->lineEdit->clear();                    // вот твоя очистка URL
    ui->lineEdit->setPlaceholderText("Введите URL...");
    ui->lineEdit->setFocus();
}




//получение урл для матч тв
void MainWindow::on_pushButton_15_clicked()
{

QUrl url("https://matchtv.ru/on-air");

    // Создаём и показываем встроенный плеер
    PlayerWindow *player = new PlayerWindow(url, this);
    player->setAttribute(Qt::WA_DeleteOnClose); // автоудаление при закрытии
    player->show();
    connect(player, &PlayerWindow::urlCaptured,
            this, [&](const QUrl& capturedUrl){
                ui->lineEdit_4->setText(capturedUrl.toString()); // Обращаемся через указатель
            });
    qDebug() << "Открыт встроенный плеер:" << url.toString();


}
void MainWindow::on_pushButton_16_clicked()
{
    QString input = ui->lineEdit_4->text();
    QString filePath = "/tmp/list.m3u";

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file) << "#EXTM3U\n" << "#EXTINF:-1,My Channel Name\n" << input << "\n";
    file.close();
    qDebug() << "Temporary M3U created at" << filePath;

    QPointer<MainWindow> safeThis(this);

    // 2️⃣ Останавливаем плеер
    QJsonObject stop;
    stop["jsonrpc"] = "2.0";
    stop["method"] = "Player.Stop";
    stop["params"] = QJsonObject{{"playerid", 1}};
    stop["id"] = rpcId++;

    sendJsonRpc(stop, "Player.Stop", [safeThis, filePath](const QJsonObject &){
        if(!safeThis) return;

        // 3️⃣ Асинхронный SFTP upload
        QtConcurrent::run([safeThis, filePath]() {
            const QString host = "192.168.8.45";
            const int port = 22;
            const QString user = "pi";
            const QString password = "639639";
            const QString remoteFile = "/var/www/html/list.m3u";

            // TCP socket
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { qDebug() << "Socket error"; return; }

            struct sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(port);
            struct hostent* he = gethostbyname(host.toUtf8().constData());
            if (!he) { ::close(sock); return; }
            sin.sin_addr = *(struct in_addr*)he->h_addr;

            if (::connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) { ::close(sock); return; }

            // libssh2 session
            LIBSSH2_SESSION* session = libssh2_session_init();
            if (!session) { ::close(sock); return; }
            if (libssh2_session_handshake(session, sock)) { libssh2_session_free(session); ::close(sock); return; }
            if (libssh2_userauth_password(session, user.toUtf8().constData(), password.toUtf8().constData())) {
                libssh2_session_disconnect(session, "Bye");
                libssh2_session_free(session);
                ::close(sock);
                return;
            }

            // SFTP
            LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
            if (!sftp) { libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }

            LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(sftp,
                remoteFile.toUtf8().constData(),
                LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);

            if (!sftpHandle) { libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }

            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) { libssh2_sftp_close(sftpHandle); libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return; }
            QByteArray data = file.readAll();
            libssh2_sftp_write(sftpHandle, data.constData(), data.size());

            libssh2_sftp_close(sftpHandle);
            libssh2_sftp_shutdown(sftp);
            libssh2_session_disconnect(session, "Bye");
            libssh2_session_free(session);
            ::close(sock);

            qDebug() << "File uploaded successfully:" << remoteFile;

            // 4️⃣ JSON-RPC в основном потоке
            if (!safeThis) return;
            QMetaObject::invokeMethod(safeThis, [safeThis]() {

                // Включаем PVR
                QJsonObject enable;
                enable["jsonrpc"] = "2.0";
                enable["method"] = "Addons.SetAddonEnabled";
                enable["params"] = QJsonObject{{"addonid","pvr.iptvsimple"}, {"enabled", true}};
                enable["id"] = safeThis->rpcId++;

                safeThis->sendJsonRpc(enable, "Enable PVR", [safeThis](const QJsonObject &){
                    if(!safeThis) return;

                    // Запуск PVR scan
                    QJsonObject scan;
                    scan["jsonrpc"]="2.0";
                    scan["method"]="PVR.Scan";
                    scan["id"]=safeThis->rpcId++;
                    safeThis->sendJsonRpc(scan,"PVR scan");

                    // Ждём появления каналов
                    auto waitForChannels = std::make_shared<std::function<void(int)>>();
                    *waitForChannels = [safeThis, waitForChannels](int attemptsLeft){
                        if(!safeThis) return;
                        QJsonObject getGroups;
                        getGroups["jsonrpc"]="2.0";
                        getGroups["method"]="PVR.GetChannelGroups";
                        getGroups["id"]=safeThis->rpcId++;

                        safeThis->sendJsonRpc(getGroups,"Get Channel Groups",[safeThis, attemptsLeft, waitForChannels](const QJsonObject &resp){
                            if(!safeThis) return;
                            QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                            if(!groups.isEmpty()) {
                                QString groupId = groups.first().toObject()["channelgroupid"].toString();

                                QJsonObject getChannels;
                                getChannels["jsonrpc"]="2.0";
                                getChannels["method"]="PVR.GetChannels";
                                getChannels["params"]=QJsonObject{
                                    {"channelgroupid", groupId},
                                    {"properties", QJsonArray{"channelnumber","label"}}
                                };
                                getChannels["id"]=safeThis->rpcId++;

                                safeThis->sendJsonRpc(getChannels,"Get Channels",[safeThis](const QJsonObject &resp){
                                    if(!safeThis) return;
                                    QJsonArray channels = resp["result"].toObject()["channels"].toArray();
                                    int channelId = 1;
                                    if(!channels.isEmpty()){
                                        channelId = channels.first().toObject()["channelid"].toInt();
                                        qDebug()<<"Opening first PVR channel:"<<channels.first().toObject()["label"].toString();
                                    }
                                    QJsonObject play;
                                    play["jsonrpc"]="2.0";
                                    play["method"]="Player.Open";
                                    play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",channelId}}}};
                                    play["id"]=safeThis->rpcId++;
                                    safeThis->sendJsonRpc(play,"Player.Open");
                                });
                            } else if(attemptsLeft>1){
                                QTimer::singleShot(1000,safeThis,[waitForChannels,attemptsLeft](){(*waitForChannels)(attemptsLeft-1);});
                            } else {
                                QJsonObject play;
                                play["jsonrpc"]="2.0";
                                play["method"]="Player.Open";
                                play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",1}}}};
                                play["id"]=safeThis->rpcId++;
                                safeThis->sendJsonRpc(play,"Player.Open");
                            }
                        });
                    };
                    (*waitForChannels)(3);
                });

            }, Qt::QueuedConnection);

        });

    });
}

void MainWindow::on_getonairnow_clicked()
{
 ui->textBrowseronairnow->clear();

    // 1. Читаем скрипт из ресурсов
    QFile src(":/match_now.py");
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->textBrowseronairnow->setHtml("<font color='red'>Ошибка: не найден скрипт<br>:/scripts/match_now.py</font>");
        return;
    }

    // 2. Создаём временный файл с правильным именем и правами (важно для FreeBSD!)
    QString scriptPath = QDir::tempPath() + "/kodigui_match_now.py";
    {
        QFile temp(scriptPath);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ui->textBrowseronairnow->append("Не могу записать временный файл в /tmp");
            return;
        }
        temp.write(src.readAll());
        temp.setPermissions(QFile::ReadOwner  | QFile::WriteOwner | QFile::ExeOwner |
                            QFile::ReadUser   | QFile::WriteUser  | QFile::ExeUser);
        temp.close();
    }

    // 3. Используем точно тот Python, который у тебя есть
    QString pythonCmd = "/usr/local/bin/python3.11";

    ui->textBrowseronairnow->append("<i>Запуск матча...</i>");
    ui->textBrowseronairnow->append("<small>" + pythonCmd + " " + scriptPath + "</small><hr>");

    // 4. Настраиваем процесс
    pythonProcess->setProcessChannelMode(QProcess::MergedChannels);

    // Живой вывод в textBrowseronairnow
    disconnect(pythonProcess, &QProcess::readyReadStandardOutput, nullptr, nullptr); // на всякий
    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        QString out = pythonProcess->readAllStandardOutput();
        ui->textBrowseronairnow->append(out.trimmed());
    });

    // 5. По завершении — удаляем файл и пишем статус
      pythonProcess->disconnect(); // чистим все старые сигналы

    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        ui->textBrowseronairnow->append(pythonProcess->readAllStandardOutput().trimmed());
    });

    connect(pythonProcess, &QProcess::readyReadStandardError, this, [this]() {
        ui->textBrowseronairnow->append("<font color='red'>" + pythonProcess->readAllStandardError().trimmed() + "</font>");
    });

    connect(pythonProcess, &QProcess::finished, this, [this, scriptPath](int exitCode) {
        if (exitCode == 0)
            ui->textBrowseronairnow->append("<hr><b><font color='green'>Матч успешно обработан</font></b>");
        else
            ui->textBrowseronairnow->append(QString("<hr><b><font color='red'>Ошибка (код %1)</font></b>").arg(exitCode));
        QFile::remove(scriptPath);
    });

    pythonProcess->start(pythonCmd, QStringList() << scriptPath);
}
