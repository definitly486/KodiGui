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
#include <QTextCursor>
#include <QDir>
#include <QTimer>
#include <QLineEdit>
#include <functional>
#include <QtConcurrent>
#include <QFuture>
#include <QPointer>
#include <QDateTime>
#include <QMetaObject>
#include <QTextEdit>
#include <QScrollBar>
#include <QCheckBox>
#include <atomic>
#include <cstdlib>
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

namespace {
QPointer<QTextEdit> debugOutput;
std::atomic_bool debugOutputEnabled{true};

void debugMessageHandler(QtMsgType type, const QMessageLogContext &context,
                         const QString &message)
{
    if (!debugOutputEnabled.load(std::memory_order_relaxed)) {
        if (type == QtFatalMsg)
            abort();
        return;
    }
    QString level;
    switch (type) {
    case QtDebugMsg:    level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg:     level = QStringLiteral("INFO"); break;
    case QtWarningMsg:  level = QStringLiteral("WARNING"); break;
    case QtCriticalMsg: level = QStringLiteral("CRITICAL"); break;
    case QtFatalMsg:    level = QStringLiteral("FATAL"); break;
    }

    QString line = QStringLiteral("[%1] [%2]")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), level);
    if (context.category && *context.category)
        line += QStringLiteral(" [%1]").arg(QString::fromLatin1(context.category));
    line += QStringLiteral(" ") + message;

    const QPointer<QTextEdit> target = debugOutput;
    if (target) {
        QMetaObject::invokeMethod(target, [target, line]() {
            if (!target)
                return;
            target->append(line.toHtmlEscaped());
            // вертикальный — в конец (новые сообщения), горизонтальный — всегда слева
            if (auto *v = target->verticalScrollBar())
                v->setValue(v->maximum());
            if (auto *h = target->horizontalScrollBar())
                h->setValue(h->minimum());
        }, Qt::QueuedConnection);
    }

    if (type == QtFatalMsg)
        abort();
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pythonProcess(new QProcess(this))        // ← обязательно this!
    , kodidlpProcess(new QProcess(this))
    , manager(new QNetworkAccessManager(this))
{
    ui->setupUi(this);

    debugOutput = ui->debugTextEdit;
    ui->debugTextEdit->document()->setMaximumBlockCount(15000);
    connect(ui->debugEnableCheckBox, &QCheckBox::toggled, this, [](bool enabled) {
        if (enabled) {
            debugOutputEnabled.store(true, std::memory_order_relaxed);
            qInfo() << "Debug output enabled";
        } else {
            qInfo() << "Debug output disabled";
            debugOutputEnabled.store(false, std::memory_order_relaxed);
        }
    });
    qInstallMessageHandler(debugMessageHandler);
    qInfo() << "KodiGui debug logging started";

    const QString buildDate = QStringLiteral(__DATE__);
    const QString buildTime = QStringLiteral(__TIME__);
    ui->buildInfoText->setHtml(
        QStringLiteral("<h2>KodiGui</h2>"
                       "<p><b>Дата сборки:</b> %1</p>"
                       "<p><b>Время сборки:</b> %2</p>"
                       "<p><b>Qt:</b> %3</p>"
                       "<p><b>Компилятор:</b> %4</p>")
            .arg(buildDate.toHtmlEscaped(), buildTime.toHtmlEscaped(),
                 QString::fromLatin1(qVersion()).toHtmlEscaped(),
#if defined(__clang__)
                 QStringLiteral("Clang ") + QStringLiteral(__clang_version__)
#elif defined(__GNUC__)
                 QStringLiteral("GCC ") + QStringLiteral(__VERSION__)
#elif defined(_MSC_VER)
                 QStringLiteral("MSVC %1").arg(_MSC_VER)
#else
                 QStringLiteral("Unknown")
#endif
                 ));

    connect(kodidlpProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromLocal8Bit(kodidlpProcess->readAllStandardOutput());
        if (!output.isEmpty()) {
            qInfo().noquote() << "[kodidlp stdout]" << output.trimmed();
            ui->textEdit_info->moveCursor(QTextCursor::End);
            ui->textEdit_info->insertPlainText(output);
        }
    });
    connect(kodidlpProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString output = QString::fromLocal8Bit(kodidlpProcess->readAllStandardError());
        if (!output.isEmpty()) {
            qWarning().noquote() << "[kodidlp stderr]" << output.trimmed();
            ui->textEdit_info->moveCursor(QTextCursor::End);
            ui->textEdit_info->insertPlainText(output);
        }
    });
    connect(kodidlpProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        qInfo() << "kodidlp finished; exitCode=" << exitCode
                 << "exitStatus=" << exitStatus;
        ui->pushButton->setEnabled(true);
        ui->pushButton->setText(QStringLiteral("run"));
        ui->textEdit_info->append(
            exitStatus == QProcess::NormalExit && exitCode == 0
                ? QStringLiteral("\n[kodidlp завершился успешно]")
                : QStringLiteral("\n[kodidlp завершился с ошибкой: %1]").arg(exitCode));
    });
    connect(kodidlpProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qCritical() << "kodidlp process error:" << error
                    << kodidlpProcess->errorString();
        if (error == QProcess::FailedToStart) {
            ui->pushButton->setEnabled(true);
            ui->pushButton->setText(QStringLiteral("run"));
            ui->textEdit_info->append(
                QStringLiteral("[Не удалось запустить kodidlp: %1]")
                    .arg(kodidlpProcess->errorString()));
        }
    });


}

MainWindow::~MainWindow()
{
    if (kodidlpProcess->state() != QProcess::NotRunning)
        kodidlpProcess->kill();

    if (pythonProcess->state() != QProcess::NotRunning) {
        pythonProcess->terminate();
        if (!pythonProcess->waitForFinished(1000))
            pythonProcess->kill();
    }
    delete ui;
}


void MainWindow::on_debugClearButton_clicked()
{
    ui->debugTextEdit->clear();
    qInfo() << "Debug log cleared";
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

    const QByteArray requestBody = QJsonDocument(json).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "[Kodi JSON-RPC request]" << desc << requestBody;
    QNetworkReply *reply = manager->post(request, requestBody);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            const QString errorText = reply->errorString();
            qWarning() << "[Kodi network error]" << desc << errorText;
            qWarning() << desc << "failed:" << errorText;
            if (onSuccess) {
                QJsonObject errorResponse;
                errorResponse["error"] = QJsonObject{
                    {"code", -1},
                    {"message", errorText}
                };
                onSuccess(errorResponse);
            }
            reply->deleteLater();
            return;
        }

        const QByteArray responseBody = reply->readAll();
        qInfo().noquote() << "[Kodi JSON-RPC response]" << desc << responseBody;
        QJsonObject response = QJsonDocument::fromJson(responseBody).object();

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
    // Kodi может аварийно завершиться, если открыть PVR-канал при
    // выключенном IPTV Simple Client. Сначала проверяем состояние аддона.
    QJsonObject check;
    check["jsonrpc"] = "2.0";
    check["id"] = rpcId++;
    check["method"] = "Addons.GetAddonDetails";
    check["params"] = QJsonObject{
        {"addonid", "pvr.iptvsimple"},
        {"properties", QJsonArray{"name", "enabled", "version"}}
    };

    sendJsonRpc(check, "Check IPTV Simple Client",
                [this, channelId](const QJsonObject &response) {
        if (response.contains("error")) {
            const QString details =
                response["error"].toObject()["message"].toString();
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client"),
                QStringLiteral("Не удалось проверить IPTV Simple Client на Kodi.%1\n\n"
                               "Воспроизведение канала отменено.")
                    .arg(details.isEmpty() ? QString() : "\n" + details));
            return;
        }

        const QJsonObject addon =
            response["result"].toObject()["addon"].toObject();
        if (addon.isEmpty() || !addon["enabled"].toBool(false)) {
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client выключен"),
                QStringLiteral("Дополнение IPTV Simple Client (pvr.iptvsimple) "
                               "не установлено или выключено.\n\n"
                               "Включите его в Kodi: Настройки → Дополнения → "
                               "Мои дополнения → Клиенты PVR → IPTV Simple Client.\n\n"
                               "Воспроизведение канала отменено, чтобы избежать "
                               "аварийного завершения Kodi."));
            return;
        }

        QJsonObject obj;
        obj["jsonrpc"] = "2.0";
        obj["id"] = rpcId++;
        obj["method"] = "Player.Open";
        obj["params"] = QJsonObject{
            {"item", QJsonObject{{"channelid", channelId}}}
        };

        sendJsonRpc(obj, QString("Player.Open channelid=%1").arg(channelId));
    });
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
    runCommand("ssh", {"pi@192.168.8.45", "killall -9 " + processName + " || true"});
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
    const QString input = on_lineEdit_textChanged().trimmed();
    if (input.isEmpty()) {
        ui->textEdit_info->append(QStringLiteral("[Введите URL для kodidlp]"));
        return;
    }

    if (kodidlpProcess->state() != QProcess::NotRunning)
        return;

    // Перед запуском kodidlp проверяем, что IPTV Simple Client включён.
    // Это предотвращает запуск обработки URL в Kodi с выключенным PVR-клиентом.
    QJsonObject check;
    check["jsonrpc"] = "2.0";
    check["id"] = rpcId++;
    check["method"] = "Addons.GetAddonDetails";
    check["params"] = QJsonObject{
        {"addonid", "pvr.iptvsimple"},
        {"properties", QJsonArray{"name", "enabled", "version"}}
    };

    ui->textEdit_info->append(QStringLiteral("[Проверка IPTV Simple Client...]"));
    ui->pushButton->setEnabled(false);
    ui->pushButton->setText(QStringLiteral("Проверка..."));

    sendJsonRpc(check, "Check IPTV Simple Client before kodidlp",
                [this, input](const QJsonObject &response) {
        if (response.contains("error")) {
            const QString details =
                response["error"].toObject()["message"].toString();
            ui->textEdit_info->append(
                QStringLiteral("[Не удалось проверить IPTV Simple Client: %1]")
                    .arg(details.isEmpty() ? QStringLiteral("неизвестная ошибка") : details));
            ui->pushButton->setEnabled(true);
            ui->pushButton->setText(QStringLiteral("run"));
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client"),
                QStringLiteral("Не удалось проверить состояние IPTV Simple Client на Kodi.\n\n"
                               "Запуск kodidlp отменён."));
            return;
        }

        const QJsonObject addon =
            response["result"].toObject()["addon"].toObject();
        if (addon.isEmpty() || !addon["enabled"].toBool(false)) {
            ui->textEdit_info->append(
                QStringLiteral("[IPTV Simple Client выключен — запуск kodidlp отменён]"));
            ui->pushButton->setEnabled(true);
            ui->pushButton->setText(QStringLiteral("run"));
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client выключен"),
                QStringLiteral("Дополнение IPTV Simple Client (pvr.iptvsimple) "
                               "не установлено или выключено.\n\n"
                               "Включите его в Kodi: Настройки → Дополнения → "
                               "Мои дополнения → Клиенты PVR → IPTV Simple Client.\n\n"
                               "Запуск kodidlp отменён."));
            return;
        }

        ui->textEdit_info->clear();
        ui->textEdit_info->append(QStringLiteral("[IPTV Simple Client включён]"));
        ui->textEdit_info->append(QStringLiteral("[Запуск kodidlp...]"));
        ui->pushButton->setText(QStringLiteral("Выполняется..."));
        kodidlpProcess->start(QStringLiteral("kodidlp"), QStringList{input});
    });
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
    qInfo() << "Starting streamlink:" << process.program() << arguments;
    process.start();

    if (!process.waitForFinished()) {
        qCritical() << "streamlink failed:" << process.errorString();
        ui->textEdit_info->setText("Ошибка выполнения процесса");
        return;
    }

    // Чтение вывода (stdout)
    QString output = process.readAllStandardOutput();
    qInfo().noquote() << "[streamlink stdout]" << output.trimmed();

    // Если нужно — читаем ошибки
    QString errorOutput = process.readAllStandardError();
    if (!errorOutput.isEmpty())
        qWarning().noquote() << "[streamlink stderr]" << errorOutput.trimmed();
    qInfo() << "streamlink finished; exitCode=" << process.exitCode();

    // Выводим в textEdit
    ui->textEdit_info->setText(output + "\n" + errorOutput);
}

QString MainWindow::on_lineEdit_textChanged()
{
     QString input = ui->lineEdit->text();

     static const QRegularExpression vksportRe(
         "^https://vksport\\.vkvideo\\.ru/(?:live|video)-(\\d+_\\d+)$");
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
    QStringList arguments;
    arguments << "-p"
              << "639639"
              << "ssh"
              << "-o"
              << "ConnectTimeout=10"
              << "pi@192.168.8.45"
              << "nohup kodi >/dev/null 2>&1 </dev/null &";

    const bool started = QProcess::startDetached("sshpass", arguments);

    if (!started) {
        qWarning() << "Start Kodi: failed to launch sshpass";
    } else {
        qDebug() << "Start Kodi: SSH launch initiated";
    }
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
    // Неблокирующий запуск: UI не замирает
    const bool ok = QProcess::startDetached(command, args);
    if (!ok) {
        qWarning() << "Не удалось запустить:" << command << args;
    } else {
        qDebug() << "Команда запущена (detached):" << command << args;
    }
}


void MainWindow::on_pushButton_8_clicked()
{
    const QString user = "pi@192.168.8.45";
    const QString input = on_lineEdit_3_textChanged().trimmed();

    if (input.isEmpty()) {
        qWarning() << "YouTube: URL пустой";
        return;
    }

    // Последовательность без блокировки UI
    runCommand("ssh", {user, "sudo systemctl start youtubeUnblock"});
    runCommand("ssh", {user, "rm -f yt.mp4"});
    runCommand("ssh", {user, "killall -9 yt-dlp || true"});
    runCommand("ssh", {user, "killall -9 ffmpeg || true"});
    runCommand("ssh", {user, "sudo systemctl restart youtubeUnblock"});

    const QString remoteCmd = QString(
        "nohup $HOME/.local/bin/yt-dlp -f 269+234 %1 --no-part -o yt.mp4 "
        "> /dev/null 2>&1 &"
    ).arg(input);

    runCommand("ssh", {user, remoteCmd});

    // Через 50 сек открываем файл в Kodi
    QTimer::singleShot(50000, this, [this]() {
        qDebug() << "Прошло 50 секунд, открываем yt.mp4";
        postPlayerOpenFile("yt.mp4");
    });
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

    // Короткая обёртка над JSON-RPC: метод + params + описание + колбэк.
    auto rpc = [safeThis](const QString &method, const QJsonObject &params,
                          const QString &desc,
                          std::function<void(const QJsonObject &)> cb) {
        if (!safeThis) return;
        QJsonObject o;
        o["jsonrpc"] = "2.0";
        o["method"]  = method;
        if (!params.isEmpty())
            o["params"] = params;
        o["id"] = safeThis->rpcId++;
        safeThis->sendJsonRpc(o, desc, cb);
    };

    // ---- Шаг 6: ждём канал в PVR и запускаем его ------------------------
    auto pollChannel = std::make_shared<std::function<void(int)>>();
    *pollChannel = [safeThis, rpc, pollChannel](int attemptsLeft) {
        if (!safeThis) return;
        auto retry = [safeThis, pollChannel, attemptsLeft](const char *why) {
            if (attemptsLeft > 1) {
                QTimer::singleShot(1500, safeThis, [pollChannel, attemptsLeft]() {
                    (*pollChannel)(attemptsLeft - 1);
                });
            } else {
                qWarning() << "Match TV: giving up:" << why;
            }
        };

        rpc("PVR.GetChannelGroups", QJsonObject{{"channeltype", "tv"}}, "Get Channel Groups",
            [safeThis, rpc, retry](const QJsonObject &resp) {
                if (!safeThis) return;
                if (resp.contains("error")) {
                    qWarning() << "Match TV: GetChannelGroups error:" << resp["error"];
                    retry("GetChannelGroups error");
                    return;
                }
                QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                if (groups.isEmpty()) {
                    retry("no channel groups");
                    return;
                }
                // channelgroupid в ответе Kodi — число, не строка.
                QJsonValue groupId = groups.first().toObject()["channelgroupid"];

                rpc("PVR.GetChannels",
                    QJsonObject{{"channelgroupid", groupId},
                                {"properties", QJsonArray{"channel"}}},
                    "Get Channels",
                    [safeThis, rpc, retry](const QJsonObject &chResp) {
                        if (!safeThis) return;
                        if (chResp.contains("error")) {
                            qWarning() << "Match TV: GetChannels error:" << chResp["error"];
                            retry("GetChannels error");
                            return;
                        }
                        QJsonArray channels = chResp["result"].toObject()["channels"].toArray();
                        if (channels.isEmpty()) {
                            retry("no channels");
                            return;
                        }
                        int channelId = channels.first().toObject()["channelid"].toInt();
                        qDebug() << "Match TV: opening channel"
                                 << channels.first().toObject()["channel"].toString();

                        rpc("Player.Open",
                            QJsonObject{{"item", QJsonObject{{"channelid", channelId}}}},
                            "Play channel",
                            [](const QJsonObject &playResp) {
                                if (playResp.contains("error"))
                                    qWarning() << "Match TV: Player.Open error:" << playResp["error"];
                                else
                                    qDebug() << "Match TV: playback started";
                            });
                    });
            });
    };

    // ---- Шаг 5: ждём, пока PVR-менеджер Kodi реально поднимется ----------
    // Любые запросы к PVR (GetChannelGroups и т.п.) в момент, когда клиент
    // ещё стартует после Enable, могут уронить Kodi. Поэтому сначала
    // опрашиваем PVR.GetProperties("available").
    auto waitPvr = std::make_shared<std::function<void(int)>>();
    *waitPvr = [safeThis, rpc, waitPvr, pollChannel](int attemptsLeft) {
        if (!safeThis) return;
        rpc("PVR.GetProperties", QJsonObject{{"properties", QJsonArray{"available"}}},
            "PVR.GetProperties",
            [safeThis, waitPvr, pollChannel, attemptsLeft](const QJsonObject &resp) {
                if (!safeThis) return;
                const bool available =
                    !resp.contains("error") && resp["result"].toObject()["available"].toBool();
                if (available) {
                    // Даём клиенту ещё секунду дочитать плейлист.
                    QTimer::singleShot(1500, safeThis, [pollChannel]() { (*pollChannel)(10); });
                } else if (attemptsLeft > 1) {
                    QTimer::singleShot(1000, safeThis, [waitPvr, attemptsLeft]() {
                        (*waitPvr)(attemptsLeft - 1);
                    });
                } else {
                    qWarning() << "Match TV: PVR did not become available, aborting";
                }
            });
    };

    // ---- Шаг 4: Enable PVR-клиента (после паузы) -------------------------
    auto enablePvr = [safeThis, rpc, waitPvr]() {
        rpc("Addons.SetAddonEnabled",
            QJsonObject{{"addonid", "pvr.iptvsimple"}, {"enabled", true}},
            "Enable PVR",
            [safeThis, waitPvr](const QJsonObject &enableResp) {
                if (!safeThis) return;
                if (enableResp.contains("error")) {
                    qWarning() << "Match TV: Enable PVR error:" << enableResp["error"];
                    return;
                }
                // PVR.Scan больше не вызываем: у iptvsimple нет «скана», он
                // отвечает -32100, а сам вызов во время старта клиента опасен.
                QTimer::singleShot(3000, safeThis, [waitPvr]() { (*waitPvr)(20); });
            });
    };

    // ---- Шаг 3: Disable PVR-клиента, затем длинная пауза -----------------
    // Disable и Enable подряд без паузы роняют Kodi: PVR-менеджер не успевает
    // остановить клиент. Ждём 4 секунды между ними.
    auto disablePvr = [safeThis, rpc, enablePvr]() {
        rpc("Addons.SetAddonEnabled",
            QJsonObject{{"addonid", "pvr.iptvsimple"}, {"enabled", false}},
            "Disable PVR",
            [safeThis, enablePvr](const QJsonObject &disableResp) {
                if (!safeThis) return;
                if (disableResp.contains("error"))
                    qWarning() << "Match TV: Disable PVR error:" << disableResp["error"];
                QTimer::singleShot(4000, safeThis, [enablePvr]() { enablePvr(); });
            });
    };

    // ---- Шаги 1-2: Stop плеера → SFTP upload → Disable ------------------
    rpc("Player.Stop", QJsonObject{{"playerid", 1}}, "Player.Stop",
        [safeThis, filePath, disablePvr](const QJsonObject &) {
            if (!safeThis) return;
            QtConcurrent::run([safeThis, filePath, disablePvr]() {
                if (!uploadFileViaSftp(filePath, "/var/www/html/list.m3u")) {
                    qWarning() << "Match TV: SFTP upload failed, aborting";
                    return;
                }
                if (!safeThis) return;
                QMetaObject::invokeMethod(
                    safeThis,
                    [safeThis, disablePvr]() {
                        if (!safeThis) return;
                        // Плеер только что остановлен — даём Kodi секунду
                        // освободить ресурсы, прежде чем трогать аддон.
                        QTimer::singleShot(1000, safeThis, [disablePvr]() { disablePvr(); });
                    },
                    Qt::QueuedConnection);
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
        if (!out.isEmpty())
            qInfo().noquote() << "[MatchTV Python stdout]" << out.trimmed();
        ui->textBrowseronairnow->append(out.trimmed());
    });

    // 5. По завершении — удаляем файл и пишем статус
      pythonProcess->disconnect(); // чистим все старые сигналы

    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = pythonProcess->readAllStandardOutput().trimmed();
        if (!out.isEmpty())
            qInfo().noquote() << "[MatchTV Python stdout]" << out;
        ui->textBrowseronairnow->append(out);
    });

    connect(pythonProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = pythonProcess->readAllStandardError().trimmed();
        if (!err.isEmpty())
            qWarning().noquote() << "[MatchTV Python stderr]" << err;
        ui->textBrowseronairnow->append("<font color='red'>" + err.toHtmlEscaped() + "</font>");
    });

    connect(pythonProcess, &QProcess::finished, this, [this, scriptPath](int exitCode) {
        qInfo() << "MatchTV Python finished; exitCode=" << exitCode;
        if (exitCode == 0)
            ui->textBrowseronairnow->append("<hr><b><font color='green'>Матч успешно обработан</font></b>");
        else
            ui->textBrowseronairnow->append(QString("<hr><b><font color='red'>Ошибка (код %1)</font></b>").arg(exitCode));
        QFile::remove(scriptPath);
    });

    qInfo() << "Starting MatchTV Python script:" << pythonCmd << scriptPath;
    pythonProcess->start(pythonCmd, QStringList() << scriptPath);
}

void MainWindow::on_playdrm_17_clicked()
{
    postPlayerOpenFile("drm/hls/master.m3u8");
}

void MainWindow::on_stopdrm_18_clicked()
{
    postPlayerStop();
}


void MainWindow::on_pushButton_rundrm_clicked()
{
    const QString user = "pi@192.168.8.45";
    QString input = on_lineEdit_drm_textChanged().trimmed();
    QString keydrm = on_lineEdit_drm_key_textChanged().trimmed();

    // Если хотя бы одно поле пустое, пытаемся взять недостающие значения
    // из ~/Downloads/widevine_key.txt: первая строка — URL, вторая — key.
    if (input.isEmpty() || keydrm.isEmpty()) {
        const QString keyFilePath =
            QDir::homePath() + "/Downloads/widevine_key.txt";
        QFile keyFile(keyFilePath);

        if (!keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "DRM",
                                 "Не удалось открыть файл:\n" + keyFilePath);
            return;
        }

        QTextStream stream(&keyFile);
        const QStringList lines = stream.readAll().split(
            QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        keyFile.close();

        if (lines.size() < 2) {
            QMessageBox::warning(this, "DRM",
                                 "В файле должны быть две строки:\n"
                                 "1 — DRM URL\n2 — DRM key");
            return;
        }

        if (input.isEmpty()) {
            input = lines.at(0).trimmed();
            ui->lineEdit_drm->setText(input);
        }
        if (keydrm.isEmpty()) {
            keydrm = lines.at(1).trimmed();
            ui->lineEdit_drm_key->setText(keydrm);
        }
    }

    if (input.isEmpty() || keydrm.isEmpty()) {
        QMessageBox::warning(this, "DRM",
                             "DRM URL или key не заполнены.");
        return;
    }

    // Все операции подготовки и запуска выполняются одной SSH-командой.
    // Это исключает гонку между несколькими независимыми startDetached().
    auto shellQuote = [](const QString &value) {
        QString escaped = value;
        escaped.replace("'", "'\\''");
        return "'" + escaped + "'";
    };

    // Dynamic HLS publisher: publishes original decrypted fMP4 fragments without remuxing.
    const QString assembleScriptBody = QString::fromUtf8(R"DRM_HLS_SCRIPT(
#!/bin/bash
set -u
LOG=/tmp/drm-mux.log
ts() { date +%H:%M:%S; }
echo "[$(ts)] Dynamic HLS publisher started" >> "$LOG"
V=""; A=""
for i in $(seq 1 90); do
  V=$(find "$HOME/drm" -mindepth 1 -maxdepth 1 -type d -name '*avc1*' 2>/dev/null | head -n 1)
  A=$(find "$HOME/drm" -mindepth 1 -maxdepth 1 -type d \( -name '*audio*' -o -name '*mp4a*' \) 2>/dev/null | head -n 1)
  if [ -n "$V" ] && [ -n "$A" ] && [ -s "$V/_init_dec.mp4" ] && [ -s "$A/_init_dec.mp4" ]; then break; fi
  sleep 1
done
if [ -z "$V" ] || [ -z "$A" ] || [ ! -s "$V/_init_dec.mp4" ] || [ ! -s "$A/_init_dec.mp4" ]; then
  echo "[$(ts)] ERROR: decrypted init segments not found" >> "$LOG"; exit 1
fi
mkdir -p "$HOME/drm/hls"
echo "[$(ts)] Found video=$V audio=$A" >> "$LOG"
LAST_V=0; LAST_A=0
while true; do
  dp=""
  [ -s "$HOME/drm_download.pid" ] && dp=$(cat "$HOME/drm_download.pid" 2>/dev/null || true)
  finished=0
  if [ -z "$dp" ] || ! kill -0 "$dp" 2>/dev/null; then finished=1; fi
  python3 - "$V" "$A" "$HOME/drm" "$finished" <<'PY_BUILDER'
import glob, math, os, pathlib, sys, tempfile, time
vdir, adir, root, finished = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3]), sys.argv[4] == '1'
out = root / 'hls'; out.mkdir(parents=True, exist_ok=True)
def files(directory):
    found = glob.glob(os.path.join(directory, '*_dec.m4s'))
    return sorted(found, key=lambda p: int(os.path.basename(p).split('_', 1)[0]))
def stable(path):
    try:
        a = os.stat(path); time.sleep(0.12); b = os.stat(path)
        return a.st_size > 0 and a.st_size == b.st_size and a.st_mtime_ns == b.st_mtime_ns
    except OSError: return False
def atomic_write(path, data):
    fd, tmp = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=str(path.parent))
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        try: os.unlink(tmp)
        except FileNotFoundError: pass
vf, af = files(vdir), files(adir)
# Match the independent track timestamps, not merely array positions.
# In this stream video ticks are 1/600 s and audio ticks are 1/48000 s.
audio_by_time = [(int(os.path.basename(p).split('_', 1)[0]) / 48000.0, p) for p in af]
pairs = []
for vp in vf:
    vt = int(os.path.basename(vp).split('_', 1)[0]) / 600.0
    if not audio_by_time: continue
    best_index = min(range(len(audio_by_time)), key=lambda i: abs(audio_by_time[i][0] - vt))
    at, ap = audio_by_time[best_index]
    if abs(at - vt) <= 0.10:
        pairs.append((vt, vp, ap))
        audio_by_time.pop(best_index)
if len(pairs) < 2: sys.exit(0)
# Never expose the newest pair until both corresponding files stop growing.
while len(pairs) >= 2 and (not stable(pairs[-1][1]) or not stable(pairs[-1][2])):
    pairs.pop()
if len(pairs) < 2: sys.exit(0)
window = 30
start = max(0, len(pairs) - window)
selected = pairs[start:]
def make_playlist(track_index, directory, done):
    durations = []
    for i, pair in enumerate(selected):
        if start + i + 1 < len(pairs): duration = pairs[start+i+1][0] - pair[0]
        elif len(selected) > 1: duration = selected[-1][0] - selected[-2][0]
        else: duration = 3.2
        if not (0.1 <= duration <= 30): duration = 3.2
        durations.append(duration)
    target = max(1, math.ceil(max(durations)))
    rel_dir = os.path.relpath(directory, out)
    lines = ['#EXTM3U','#EXT-X-VERSION:7',f'#EXT-X-TARGETDURATION:{target}',
             f'#EXT-X-MEDIA-SEQUENCE:{start}',f'#EXT-X-MAP:URI="{rel_dir}/_init_dec.mp4"']
    for pair, duration in zip(selected, durations):
        name = os.path.basename(pair[track_index])
        lines += [f'#EXTINF:{duration:.3f},', f'{rel_dir}/{name}']
    if done: lines.append('#EXT-X-ENDLIST')
    return '\n'.join(lines)+'\n'
atomic_write(out/'video.m3u8', make_playlist(1, pathlib.Path(vdir), finished))
atomic_write(out/'audio.m3u8', make_playlist(2, pathlib.Path(adir), finished))
master = ('#EXTM3U\n#EXT-X-VERSION:7\n'
          '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="AAC",DEFAULT=YES,AUTOSELECT=YES,URI="audio.m3u8"\n'
          '#EXT-X-STREAM-INF:BANDWIDTH=700000,CODECS="avc1.4D401E,mp4a.40.2",AUDIO="audio"\nvideo.m3u8\n')
atomic_write(out/'master.m3u8',master)
print(f'published {len(pairs)} timestamp-matched pairs, finished={finished}')
PY_BUILDER
  rc=$?
  if [ "$rc" -ne 0 ]; then echo "[$(ts)] ERROR: playlist publisher exit=$rc" >> "$LOG"; fi
  vc=$(find "$V" -maxdepth 1 -name '*_dec.m4s' 2>/dev/null | wc -l)
  ac=$(find "$A" -maxdepth 1 -name '*_dec.m4s' 2>/dev/null | wc -l)
  if [ "$vc" -ne "$LAST_V" ] || [ "$ac" -ne "$LAST_A" ]; then
    echo "[$(ts)] HLS updated: video=$vc audio=$ac" >> "$LOG"; LAST_V=$vc; LAST_A=$ac
  fi
  if [ "$finished" -eq 1 ]; then echo "[$(ts)] Downloader finished; final HLS playlist published" >> "$LOG"; break; fi
  sleep 1
done
echo "[$(ts)] HLS publisher finished" >> "$LOG"
)DRM_HLS_SCRIPT");

    const QString remoteCmd =
        // Останавливаем старые процессы, затем только после этого удаляем старые файлы.
        "if [ -s \"$HOME/drm_mux.pid\" ]; then "
        "  old_mux=$(cat \"$HOME/drm_mux.pid\"); "
        "  kill -TERM -- -\"$old_mux\" 2>/dev/null || true; "
        "fi; "
        "if [ -s \"$HOME/drm_download.pid\" ]; then "
        "  old_dl=$(cat \"$HOME/drm_download.pid\"); "
        "  kill -TERM \"$old_dl\" 2>/dev/null || true; "
        "fi; "
        "killall -9 N_m3u8DL-RE 2>/dev/null || true; "
        "sleep 1; "
        "rm -f \"$HOME/drm_mux.pid\" \"$HOME/drm_download.pid\" "
        "      \"$HOME/drm.mp4\" \"$HOME/master.m3u8\" \"$HOME/drm.ts\" \"$HOME/drm.aar.ts\" "
        "      /tmp/drm-assemble.sh; "
        "rm -rf \"$HOME/drm\"; "
        "mkdir -p \"$HOME/drm\"; "
        ": > /tmp/drm-download.log; "
        ": > /tmp/drm-mux.log; "
        // Пишем скрипт сборщика в файл (без проблем с кавычками).
        "cat > /tmp/drm-assemble.sh << 'ASSEMBLE_EOF'\n" +
        assembleScriptBody +
        "ASSEMBLE_EOF\n"
        "chmod +x /tmp/drm-assemble.sh; "
        // Загрузчик: расшифрованные сегменты в $HOME/drm (без live-pipe-mux).
        "nohup N_m3u8DL-RE " + shellQuote(input) +
        " --key " + shellQuote(keydrm) +
        " -sv worst "
        "--save-name drm --save-dir \"$HOME/drm\" "
        "--mp4-real-time-decryption "
        "--live-keep-segments "
        "--select-audio id='audio_aar=128000' "
        "> /tmp/drm-download.log 2>&1 < /dev/null & "
        "echo $! > \"$HOME/drm_download.pid\"; "
        // Публикатор динамических HLS-плейлистов → drm/hls/master.m3u8
        "nohup setsid /tmp/drm-assemble.sh "
        "> /tmp/drm-mux.log 2>&1 < /dev/null & "
        "echo $! > \"$HOME/drm_mux.pid\"";

    runCommand("ssh", {user, remoteCmd});

    // Проверяем появление непустого drm.mp4 и состояние группы сборщика.
    if (drmFileCheckTimer)
        drmFileCheckTimer->stop();
    if (drmFileCheckProcess && drmFileCheckProcess->state() != QProcess::NotRunning)
        drmFileCheckProcess->kill();

    if (!drmFileCheckProcess) {
        drmFileCheckProcess = new QProcess(this);
        connect(drmFileCheckProcess,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            const QString output =
                QString::fromUtf8(drmFileCheckProcess->readAllStandardOutput());
            if (!output.isEmpty())
                qInfo().noquote() << "[DRM remote check]" << output.trimmed();
            qInfo() << "DRM remote check finished; exitCode=" << exitCode;

            if (exitCode == 0 && output.contains("DRM_MP4_READY")) {
                if (drmFileCheckTimer)
                    drmFileCheckTimer->stop();
                QMessageBox::information(this, "DRM",
                    "Динамический HLS-плейлист готов.\n"
                    "Нажмите Play — Kodi откроет поток один раз\n"
                    "и будет получать новые сегменты по мере загрузки.");
            } else if (output.contains("DRM_MUX_FAILED")) {
                if (drmFileCheckTimer)
                    drmFileCheckTimer->stop();
                QMessageBox::warning(this, "DRM",
                    "Публикатор HLS завершился до создания плейлиста.\n\n"
                    "Последние строки журнала:\n" + output);
            }
        });
    }

    if (!drmFileCheckTimer) {
        drmFileCheckTimer = new QTimer(this);
        drmFileCheckTimer->setInterval(1500);
        connect(drmFileCheckTimer, &QTimer::timeout, this, [this]() {
            if (drmFileCheckProcess->state() != QProcess::NotRunning)
                return;

            // Ограничиваем ожидание, чтобы при ошибке SSH/загрузчика не проверять бесконечно.
            if (++drmFileCheckAttempts > 80) {
                drmFileCheckTimer->stop();
                QMessageBox::warning(this, "DRM",
                    "За ~2 минуты HLS-плейлист не появился.\n"
                    "Проверьте /tmp/drm-download.log и /tmp/drm-mux.log на Raspberry Pi.");
                return;
            }

            {
                qInfo() << "Checking remote DRM output and assembler status";
                drmFileCheckProcess->start("ssh", {
                    "pi@192.168.8.45",
                    "if [ -s \"$HOME/drm/hls/master.m3u8\" ]; then "
                    "  echo DRM_MP4_READY; "
                    "elif [ -s \"$HOME/drm_mux.pid\" ]; then "
                    "  p=$(cat \"$HOME/drm_mux.pid\"); "
                    "  if ! kill -0 -- -\"$p\" 2>/dev/null; then "
                    "    echo DRM_MUX_FAILED; "
                    "    tail -n 25 /tmp/drm-mux.log; "
                    "  fi; "
                    "fi"
                });
            }
        });
    }
    drmFileCheckAttempts = 0;
    drmFileCheckTimer->start();

    qDebug() << "DRM: N_m3u8DL-RE + dynamic HLS publisher started";
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


void MainWindow::on_pushButton_playdrm_clicked()
{
    postPlayerOpenFile("drm/hls/master.m3u8");
}


void MainWindow::on_pushButton_kill_m3u8DL_clicked()
{
    if (drmFileCheckTimer)
        drmFileCheckTimer->stop();
    if (drmFileCheckProcess && drmFileCheckProcess->state() != QProcess::NotRunning)
        drmFileCheckProcess->kill();

    // Остановка загрузчика и сборщика сегментов на удалённом устройстве.
    runCommand("ssh", {"pi@192.168.8.45",
        "if [ -s \"$HOME/drm_mux.pid\" ]; then "
        "  p=$(cat \"$HOME/drm_mux.pid\"); "
        "  kill -TERM -- -\"$p\" 2>/dev/null || true; "
        "fi; "
        "if [ -s \"$HOME/drm_download.pid\" ]; then "
        "  p=$(cat \"$HOME/drm_download.pid\"); "
        "  kill -TERM \"$p\" 2>/dev/null || true; "
        "fi; "
        "killall -9 N_m3u8DL-RE 2>/dev/null || true; "
        "rm -f \"$HOME/drm_mux.pid\" \"$HOME/drm_download.pid\" "
        "      /tmp/drm-assemble.sh"});
}


void MainWindow::on_pushButton_17_clicked()
{
    postInputAction("back");
}



void MainWindow::on_pushButton_18_clicked()
{
    QProcess *process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [=]() {
        QByteArray data = process->readAllStandardOutput();
        if (!data.isEmpty())
            qInfo().noquote() << "[Monitor stdout]" << QString::fromUtf8(data).trimmed();
        ui->textEdit->append(QString::fromUtf8(data));
    });

    connect(process, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray data = process->readAllStandardError();
        if (!data.isEmpty())
            qWarning().noquote() << "[Monitor stderr]" << QString::fromUtf8(data).trimmed();
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


