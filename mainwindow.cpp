
#include "ui_kodigui.h"
#include "mainwindow.h"
#include "taskqueue.h"
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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pythonProcess(new QProcess(this))        // ← обязательно this!
    , queue(new TaskQueue(this))
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

     reloadPvrIptvSimple(mgr, 2);

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
reloadPvrIptvSimple(mgr, 2);

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

    // Создаем файл
    QFile file("/tmp/list.m3u");
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << input << "|user-agent=Mozilla/5.0 (X11; FreeBSD amd64; rv:77.0) Gecko/20100101 Firefox/77.0\n";
        file.close();
    } else {
        qDebug() << "Cannot create /tmp/list.m3u";
        return;
    }

    TaskQueue* queue = new TaskQueue(this);

    QUrl jsonRpcUrl("http://192.168.8.45:8081/jsonrpc");

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Player.Stop"},{"params", QJsonObject{{"playerid",1}}},{"id",1}}));

    queue->addTask(new SftpTask("192.168.8.45", 22, "pi", "639639",
                                "/tmp/list.m3u", "/var/www/html/list.m3u"));

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Addons.SetAddonEnabled"},{"params", QJsonObject{{"addonid","pvr.iptvsimple"},{"enabled","toggle"}}},{"id",1}}));

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Player.Open"},{"params", QJsonObject{{"item", QJsonObject{{"channelid",1}}}}},{"id",1}}));

    queue->start();

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


//запуск матч тв
void MainWindow::on_pushButton_16_clicked()
{

    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    reloadPvrIptvSimple(mgr, 2);

    QString input = ui->lineEdit_4->text();

    // Создаем файл
    QFile file("/tmp/list.m3u");
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << input << "|user-agent=Mozilla/5.0 (X11; FreeBSD amd64; rv:77.0) Gecko/20100101 Firefox/77.0\n";
        file.close();
    } else {
        qDebug() << "Cannot create /tmp/list.m3u";
        return;
    }

    TaskQueue* queue = new TaskQueue(this);

    QUrl jsonRpcUrl("http://192.168.8.45:8081/jsonrpc");

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Player.Stop"},{"params", QJsonObject{{"playerid",1}}},{"id",1}}));

    queue->addTask(new DelayTask(3000, queue)); // <-- sleep 3s

    queue->addTask(new SftpTask("192.168.8.45", 22, "pi", "639639",
                                "/tmp/list.m3u", "/var/www/html/list.m3u"));

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Addons.SetAddonEnabled"},{"params", QJsonObject{{"addonid","pvr.iptvsimple"},{"enabled","toggle"}}},{"id",1}}));

    queue->addTask(new JsonRpcTask(jsonRpcUrl, {{"jsonrpc","2.0"},{"method","Player.Open"},{"params", QJsonObject{{"item", QJsonObject{{"channelid",1}}}}},{"id",1}}));

    queue->start();

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

// Функция отправки JSON-RPC запроса для включения/отключения аддона
void sendToggleAddon(QNetworkAccessManager* mgr, int id = 1)
{
    const QUrl url(QStringLiteral("http://192.168.8.45:8081/jsonrpc"));

    QJsonObject params;
    params["addonid"] = "pvr.iptvsimple";
    params["enabled"] = "toggle";

    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = "Addons.SetAddonEnabled";
    obj["params"] = params;
    obj["id"] = id;

    QByteArray data = QJsonDocument(obj).toJson();

    // Логируем URL и тело запроса
    qDebug() << "[sendToggleAddon] URL:" << url.toString();
    qDebug() << "[sendToggleAddon] JSON-RPC данные:" << QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = mgr->post(request, data);

    QObject::connect(reply, &QNetworkReply::finished, [reply, id]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString contents = QString::fromUtf8(reply->readAll());
            qDebug() << "[sendToggleAddon] Ответ от Kodi для вызова" << id << ":" << contents;
        } else {
            qDebug() << "[sendToggleAddon] Ошибка для вызова" << id << ":" << reply->errorString();
        }
        reply->deleteLater();
    });
}

// Функция повторного вызова с задержкой 3 секунды
void MainWindow::reloadPvrIptvSimple(QNetworkAccessManager* mgr, int times)
{
    qDebug() << "[reloadPvrIptvSimple] Запуск перезагрузки pvr.iptvsimple, повторов:" << times;

    for (int i = 0; i < times; ++i) {
        QTimer::singleShot(i * 3000, [ mgr, i, times]() {
            qDebug() << "[reloadPvrIptvSimple] Выполняем вызов" << i+1 << "/" << times;
            sendToggleAddon(mgr, i+1); // твоя функция для отправки JSON
        });
    }
}
