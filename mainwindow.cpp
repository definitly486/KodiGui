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
#include <QUrl>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QLineEdit>
#include <functional>
#include <QtConcurrent>
#include <QFuture>
#include <QPointer>
#include <libssh2.h>
#include <libssh2_sftp.h>

// Выполняет внешнюю команду (используется для "ssh pi@... <cmd>").
// Определена ниже в файле; объявлена здесь, т.к. используется раньше.
void runCommand(const QString &command, const QStringList &args = {});
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


void MainWindow::on_horizontalSlider_valueChanged(int value)
{
    postSetVolume(value);
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

// ---------------------------------------------------------------------
// Общие хелперы для Kodi JSON-RPC.
// Раньше каждый обработчик кнопки сам создавал QNetworkAccessManager,
// собирал QJsonObject и слушал QNetworkReply::finished — один и тот же
// код повторялся более десятка раз. Теперь все они сводятся к sendJsonRpc().
// ---------------------------------------------------------------------

void MainWindow::postPlayerOpenFile(const QString &file)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Player.Open";
    obj["params"] = QJsonObject{{"item", QJsonObject{{"file", file}}}};

    sendJsonRpc(obj, "Player.Open file=" + file);
}

void MainWindow::postPlayerOpenChannel(int channelId)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Player.Open";
    obj["params"] = QJsonObject{{"item", QJsonObject{{"channelid", channelId}}}};

    sendJsonRpc(obj, QString("Player.Open channelid=%1").arg(channelId));
}

void MainWindow::postPlayerStop()
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Player.Stop";
    obj["params"] = QJsonObject{{"playerid", 1}};

    sendJsonRpc(obj, "Player.Stop");
}

void MainWindow::postSetVolume(int volume)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Application.SetVolume";
    obj["params"] = QJsonObject{{"volume", volume}};

    sendJsonRpc(obj, QString("Application.SetVolume=%1").arg(volume));
}

void MainWindow::postInputAction(const QString &action)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Input.ExecuteAction";
    obj["params"] = QJsonObject{{"action", action}};

    sendJsonRpc(obj, "Input.ExecuteAction action=" + action);
}

void MainWindow::postSetAddonEnabled(const QString &addonId, const QJsonValue &enabledValue)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Addons.SetAddonEnabled";
    obj["params"] = QJsonObject{{"addonid", addonId}, {"enabled", enabledValue}};

    sendJsonRpc(obj, "Addons.SetAddonEnabled " + addonId);
}

// ---------------------------------------------------------------------
// Общий хелпер для SSH-команд "убить процесс на Pi". Раньше на каждую
// такую кнопку заводился отдельный обработчик с лямбдой executeSequence,
// внутри которой был один-единственный вызов runCommand.
// ---------------------------------------------------------------------
void MainWindow::sshKillProcess(const QString &processName)
{
    runCommand("ssh", {"pi@192.168.8.45", "killall -9 " + processName});
}

// ---------------------------------------------------------------------
// Общий хелпер очистки текстового поля с плейсхолдером.
// ---------------------------------------------------------------------
void MainWindow::clearLineEditField(QLineEdit *edit, const QString &placeholder)
{
    edit->clear();
    edit->setPlaceholderText(placeholder);
    edit->setFocus();
}

// ---------------------------------------------------------------------
// Синхронно заливает локальный файл на Pi по SFTP через сырой TCP-сокет
// и libssh2. Раньше этот ~70-строчный блок был продублирован дословно
// в двух обработчиках (on_pushButton_7_clicked и on_pushButton_16_clicked).
// Предназначена для вызова из фонового потока (QtConcurrent::run), как и раньше.
// ---------------------------------------------------------------------
bool uploadFileViaSftp(const QString &localPath, const QString &remoteFile)
{
    const QString host = "192.168.8.45";
    const int port = 22;
    const QString user = "pi";
    const QString password = "639639";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { qDebug() << "Socket error"; return false; }

    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    struct hostent* he = gethostbyname(host.toUtf8().constData());
    if (!he) { ::close(sock); return false; }
    sin.sin_addr = *(struct in_addr*)he->h_addr;

    if (::connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) { ::close(sock); return false; }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) { ::close(sock); return false; }
    if (libssh2_session_handshake(session, sock)) { libssh2_session_free(session); ::close(sock); return false; }
    if (libssh2_userauth_password(session, user.toUtf8().constData(), password.toUtf8().constData())) {
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        ::close(sock);
        return false;
    }

    LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
    if (!sftp) { libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return false; }

    LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(sftp,
        remoteFile.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);

    if (!sftpHandle) { libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return false; }

    bool ok = false;
    QFile file(localPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        libssh2_sftp_write(sftpHandle, data.constData(), data.size());
        ok = true;
    }

    libssh2_sftp_close(sftpHandle);
    libssh2_sftp_shutdown(sftp);
    libssh2_session_disconnect(session, "Bye");
    libssh2_session_free(session);
    ::close(sock);

    if (ok)
        qDebug() << "File uploaded successfully:" << remoteFile;
    return ok;
}

void MainWindow::on_pushButton_clicked()
{
    QString input = on_lineEdit_textChanged();

    qDebug() << input;

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

void MainWindow::on_pushButton_info_clicked()
{
    // Получаем текст из lineEdit (с заменой vksport-ссылок на vk.ru)
    QString input = on_lineEdit_textChanged();

    qDebug() << input;

    QProcess process;

    // Формируем аргументы для streamlink
    QStringList arguments;
    arguments << input;

    // Указываем программу
    process.setProgram("streamlink");
    process.setArguments(arguments);

    // Запуск процесса
    process.start();

    if (!process.waitForFinished()) {
        ui->textEdit_info->setText("Ошибка выполнения процесса");
        return;
    }

    // Чтение вывода (stdout)
    QString output = process.readAllStandardOutput();

    // Если нужно — читаем ошибки
    QString errorOutput = process.readAllStandardError();

    // Выводим в textEdit
    ui->textEdit_info->setText(output + "\n" + errorOutput);
}

QString MainWindow::on_lineEdit_textChanged()
{
     QString input = ui->lineEdit->text();

     static const QRegularExpression vksportRe(
         "^https://vksport\\.vkvideo\\.ru/live-(\\d+_\\d+)$");
     QRegularExpressionMatch match = vksportRe.match(input);
     if (match.hasMatch()) {
         input = "https://vk.ru/video-" + match.captured(1);
     }

     return input;
}

void MainWindow::on_pushButton_2_clicked()
{
    postPlayerOpenChannel(1);
}


void MainWindow::on_pushButton_3_clicked()
{
    postPlayerStop();
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
            if (!uploadFileViaSftp(filePath, "/var/www/html/list.m3u"))
                return;

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
    postPlayerOpenFile("yt.mp4");
}


void runCommand(const QString &command, const QStringList &args) {
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
       runCommand(sshPrefix, {user, "$HOME/.local/bin/yt-dlp  -f 91 " + input + " --no-part   -o yt.mp4 " " > /dev/null 2>&1 &"});
        // 7. sleep 50
        QTimer::singleShot(50000, this, [this]() {
            qDebug() << "Прошло 50 секунд.";
            postPlayerOpenFile("yt.mp4");
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
    postPlayerStop();
}


void MainWindow::on_pushButton_11_clicked()
{
    sshKillProcess("yt-dlp");
}


void MainWindow::on_pushButton_12_clicked()
{
    sshKillProcess("ffmpeg");
}


void MainWindow::on_pushButton_13_clicked()
{
    postSetAddonEnabled("pvr.iptvsimple", QJsonValue("toggle"));
}


void MainWindow::on_pushButton_14_clicked()
{
    postInputAction("back");
}


void MainWindow::on_pushButton_clearurl_clicked()
{
    clearLineEditField(ui->lineEdit_2, "Введите URL...");
    clearLineEditField(ui->lineEdit, "Введите URL...");
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

    if (input.trimmed().isEmpty()) {
        qWarning() << "Match TV: URL is empty — press \"Получить URL\" first";
        return;
    }

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file)
        << "#EXTM3U\n"
        << "#EXTINF:-1,My Channel Name\n"
        << input << "\n";
    file.close();

    qDebug() << "Temporary M3U created at" << filePath;

    QPointer<MainWindow> safeThis(this);

    // 2️⃣ Останавливаем плеер
    QJsonObject stop;
    stop["jsonrpc"] = "2.0";
    stop["method"]  = "Player.Stop";
    stop["params"]  = QJsonObject{{"playerid", 1}};
    stop["id"]      = rpcId++;

    sendJsonRpc(stop, "Player.Stop",
                [safeThis, filePath](const QJsonObject &) {
                    if (!safeThis) return;

                    // 3️⃣ Асинхронный SFTP upload
                    QtConcurrent::run([safeThis, filePath]() {
                        if (!uploadFileViaSftp(filePath, "/var/www/html/list.m3u")) {
                            qWarning() << "Match TV: SFTP upload failed, aborting";
                            return;
                        }

                        // 4️⃣ Возврат в GUI-поток
                        if (!safeThis) return;
                        QMetaObject::invokeMethod(
                            safeThis,
                            [safeThis]() {
                                if (!safeThis) return;

                                // Перезапуск PVR-клиента: если аддон уже был включён,
                                // повторный Enable — это no-op и plist.m3u не перечитывается.
                                // Поэтому сначала гарантированно выключаем...
                                QJsonObject disable;
                                disable["jsonrpc"] = "2.0";
                                disable["method"]  = "Addons.SetAddonEnabled";
                                disable["params"]  = QJsonObject{
                                    {"addonid", "pvr.iptvsimple"},
                                    {"enabled", false}
                                };
                                disable["id"] = safeThis->rpcId++;

                                safeThis->sendJsonRpc(
                                    disable,
                                    "Disable PVR",
                                    [safeThis](const QJsonObject &disableResp) {
                                        if (!safeThis) return;
                                        if (disableResp.contains("error")) {
                                            qWarning() << "Match TV: Disable PVR error:" << disableResp["error"];
                                            // не фатально — пробуем включить всё равно
                                        }

                                        // ...а затем включаем заново — только тогда клиент
                                        // пересоздаётся и подхватывает новый list.m3u с сервера.
                                        QJsonObject enable;
                                        enable["jsonrpc"] = "2.0";
                                        enable["method"]  = "Addons.SetAddonEnabled";
                                        enable["params"]  = QJsonObject{
                                            {"addonid", "pvr.iptvsimple"},
                                            {"enabled", true}
                                        };
                                        enable["id"] = safeThis->rpcId++;

                                        safeThis->sendJsonRpc(
                                            enable,
                                            "Enable PVR",
                                            [safeThis](const QJsonObject &enableResp) {
                                        if (!safeThis) return;
                                        if (enableResp.contains("error")) {
                                            qWarning() << "Match TV: Enable PVR error:" << enableResp["error"];
                                            return;
                                        }

                                        // PVR.Scan — НЕ трогаем SetAddonEnabled(false/true) вокруг
                                        // него: быстрый disable/enable аддона во время скана —
                                        // это race condition, из-за которой иногда падал Kodi.
                                        QJsonObject scan;
                                        scan["jsonrpc"] = "2.0";
                                        scan["method"]  = "PVR.Scan";
                                        scan["id"]      = safeThis->rpcId++;

                                        safeThis->sendJsonRpc(
                                            scan,
                                            "PVR.Scan",
                                            [safeThis](const QJsonObject &scanResp) {
                                                if (!safeThis) return;
                                                if (scanResp.contains("error")) {
                                                    // PVR.Scan часто отдаёт -32100 сразу после
                                                    // включения аддона (PVR-менеджер ещё не успел
                                                    // зарегистрировать бэкенд) — это не фатально,
                                                    // список каналов обычно всё равно появляется.
                                                    // Не прерываем цепочку, просто логируем и идём
                                                    // дальше в polling.
                                                    qWarning() << "Match TV: PVR.Scan error (non-fatal, continuing):" << scanResp["error"];
                                                }

                                                // ===== Опрашиваем PVR, пока канал реально не
                                                // появится, вместо угадывания задержки =====
                                                auto waitForChannel = std::make_shared<std::function<void(int)>>();
                                                *waitForChannel = [safeThis, waitForChannel](int attemptsLeft) {
                                                    if (!safeThis) return;

                                                    QJsonObject getGroups;
                                                    getGroups["jsonrpc"] = "2.0";
                                                    getGroups["method"]  = "PVR.GetChannelGroups";
                                                    getGroups["params"]  = QJsonObject{
                                                        {"channeltype", "tv"}
                                                    };
                                                    getGroups["id"]      = safeThis->rpcId++;

                                                    safeThis->sendJsonRpc(
                                                        getGroups, "Get Channel Groups",
                                                        [safeThis, attemptsLeft, waitForChannel](const QJsonObject &resp) {
                                                            if (!safeThis) return;
                                                            if (resp.contains("error")) {
                                                                qWarning() << "Match TV: GetChannelGroups error:" << resp["error"];
                                                                return;
                                                            }

                                                            QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                                                            if (!groups.isEmpty()) {
                                                                // channelgroupid в ответе Kodi — число,
                                                                // а не строка; toString() на нём вернул бы
                                                                // "" и ломал GetChannels ниже.
                                                                QJsonValue groupId = groups.first().toObject()["channelgroupid"];

                                                                QJsonObject getChannels;
                                                                getChannels["jsonrpc"] = "2.0";
                                                                getChannels["method"]  = "PVR.GetChannels";
                                                                getChannels["params"]  = QJsonObject{
                                                                    {"channelgroupid", groupId},
                                                                    {"properties", QJsonArray{"channel"}}
                                                                };
                                                                getChannels["id"] = safeThis->rpcId++;

                                                                safeThis->sendJsonRpc(
                                                                    getChannels, "Get Channels",
                                                                    [safeThis, attemptsLeft, waitForChannel](const QJsonObject &chResp) {
                                                                        if (!safeThis) return;
                                                                        if (chResp.contains("error")) {
                                                                            qWarning() << "Match TV: GetChannels error:" << chResp["error"];
                                                                            return;
                                                                        }

                                                                        QJsonArray channels = chResp["result"].toObject()["channels"].toArray();
                                                                        if (channels.isEmpty()) {
                                                                            if (attemptsLeft > 1) {
                                                                                QTimer::singleShot(1000, safeThis, [waitForChannel, attemptsLeft]() {
                                                                                    (*waitForChannel)(attemptsLeft - 1);
                                                                                });
                                                                            } else {
                                                                                qWarning() << "Match TV: no channels appeared after scan, giving up";
                                                                            }
                                                                            return;
                                                                        }

                                                                        int channelId = channels.first().toObject()["channelid"].toInt();
                                                                        qDebug() << "Match TV: opening channel"
                                                                                 << channels.first().toObject()["channel"].toString();

                                                                        QJsonObject play;
                                                                        play["jsonrpc"] = "2.0";
                                                                        play["method"]  = "Player.Open";
                                                                        play["params"]  = QJsonObject{
                                                                            {"item", QJsonObject{{"channelid", channelId}}}
                                                                        };
                                                                        play["id"] = safeThis->rpcId++;

                                                                        safeThis->sendJsonRpc(
                                                                            play, "Play channel",
                                                                            [](const QJsonObject &playResp) {
                                                                                if (playResp.contains("error"))
                                                                                    qWarning() << "Match TV: Player.Open error:" << playResp["error"];
                                                                                else
                                                                                    qDebug() << "Match TV: playback started";
                                                                            });
                                                                    });
                                                            } else if (attemptsLeft > 1) {
                                                                QTimer::singleShot(1000, safeThis, [waitForChannel, attemptsLeft]() {
                                                                    (*waitForChannel)(attemptsLeft - 1);
                                                                });
                                                            } else {
                                                                qWarning() << "Match TV: no channel groups appeared after scan, giving up";
                                                            }
                                                        });
                                                };
                                                // Небольшая пауза перед первой попыткой — даём
                                                // скану/бэкенду время на инициализацию.
                                                QTimer::singleShot(500, safeThis, [waitForChannel]() {
                                                    (*waitForChannel)(5);
                                                });
                                            });
                                    });
                                    });
                            },
                            Qt::QueuedConnection
                            );
                    });
                }
                );
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

void MainWindow::on_playdrm_17_clicked()
{
    postPlayerOpenFile("drm.mp4");
}

void MainWindow::on_stopdrm_18_clicked()
{
    postPlayerStop();
}


void MainWindow::on_pushButton_rundrm_clicked()
{

    QString sshPrefix = "ssh";
    QString user = "pi@192.168.8.45";

    // Последовательное выполнение команд с задержками
    auto executeSequence = [&]() {

         //3 
         sshKillProcess("N_m3u8DL-RE");
        //4 
        runCommand(sshPrefix, {user, "rm -R $HOME/drm.ts"});
        //5

         runCommand(sshPrefix, {user, "rm -R $HOME/drm"});
        
        // 6. N_m3u8DL-RE
        QString input = on_lineEdit_drm_textChanged();
        QString keydrm = on_lineEdit_drm_key_textChanged();

        // N_m3u8DL-RE $1 -M format=mp4 --key  $2 -sv worst    --save-name "drm" --save-dir  $HOME  --live-pipe-mux   --select-audio id="audio_aar=128000"
        runCommand(sshPrefix, {user, "N_m3u8DL-RE "+input+" -M format=mp4 --key  "+keydrm+" -sv worst    --save-name drm --save-dir  $HOME  --live-pipe-mux   --select-audio id='audio_aar=128000' " " > /dev/null 2>&1 &"});
        // 7. sleep 50
        QTimer::singleShot(50000, []() {
            qDebug() << "Прошло 50 секунд.";
            // Можно добавить дальнейшие действия после ожидания


        });



    };

    // Запуск последовательности
    executeSequence();


}


QString MainWindow::on_lineEdit_drm_textChanged()
{
    QString input = ui->lineEdit_drm->text();
    return input;
}


QString MainWindow::on_lineEdit_drm_key_textChanged()
{
    QString input = ui->lineEdit_drm_key->text();
    return input;
}


void MainWindow::on_pushButton_killstreamlink_clicked()
{
    sshKillProcess("streamlink");
}


void MainWindow::on_pushButton_cleardrm_clicked()
{
    clearLineEditField(ui->lineEdit_drm, "Введите URL...");
    clearLineEditField(ui->lineEdit_drm_key, "Введите key...");
}


void MainWindow::on_pushButton_playdrmts_clicked()
{
    postPlayerOpenFile("drm.ts");
}


void MainWindow::on_pushButton_kill_m3u8DL_clicked()
{
    sshKillProcess("N_m3u8DL-RE");
}


void MainWindow::on_pushButton_17_clicked()
{
    postInputAction("back");
}


void MainWindow::on_pushButton_playdrmaarts_clicked()
{
    postPlayerOpenFile("drm.aar.ts");
}


void MainWindow::on_pushButton_18_clicked()
{
    QProcess *process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [=]()
            {
                QByteArray data = process->readAllStandardOutput();
                ui->textEdit->append(QString::fromUtf8(data));
            });

    connect(process, &QProcess::readyReadStandardError, this, [=]()
            {
                QByteArray data = process->readAllStandardError();
                ui->textEdit->append(QString::fromUtf8(data));
            });

    QString program = "sshpass";
    QStringList arguments;

    arguments << "-p" << "639639"
              << "ssh"
              << "pi@192.168.8.45"
              << "top -b -n 1";
    ui->textEdit->clear();
    process->start(program, arguments);
}


