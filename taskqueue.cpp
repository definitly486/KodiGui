#include "taskqueue.h"
#include <QTimer>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

// libssh2
#include <libssh2.h>         // Основной заголовок (session, handshake и т.д.)
#include <libssh2_sftp.h>    // Для SFTP

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>


// ------------------ JsonRpcTask ------------------
JsonRpcTask::JsonRpcTask(const QUrl &url, const QJsonObject &json)
    : m_url(url), m_json(json) {}

void JsonRpcTask::execute() {
    static QNetworkAccessManager manager;
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonDocument doc(m_json);
    QByteArray data = doc.toJson();
    QNetworkReply* reply = manager.post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [this, reply]() {
        if(reply->error() == QNetworkReply::NoError) {
            qDebug() << "JSON-RPC Success:" << reply->readAll();
        } else {
            qDebug() << "JSON-RPC Error:" << reply->errorString();
        }
        reply->deleteLater();
        emit finished();
    });
}

// ------------------ SftpTask ------------------
SftpTask::SftpTask(const QString& host, int port,
                   const QString& user, const QString& password,
                   const QString& localFile, const QString& remoteFile)
    : m_host(host), m_port(port), m_user(user), m_password(password),
      m_localFile(localFile), m_remoteFile(remoteFile) {}

void SftpTask::execute() {
    QThread* thread = QThread::create([this]() {
        uploadFile();
        emit finished();
    });
    thread->start();
}

void SftpTask::uploadFile() {
    // 1. Создаем TCP сокет
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        qDebug() << "Socket error";
        return;
    }

    // 2. Настраиваем sockaddr_in
    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(m_port);

    struct hostent *he = gethostbyname(m_host.toStdString().c_str());
    if (!he) {
        qDebug() << "Host not found";
        close(sock);
        return;
    }
    sin.sin_addr = *(struct in_addr*)he->h_addr;

    // 3. Подключаемся к серверу
    if (::connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        qDebug() << "Connect failed";
        close(sock);
        return;
    }

    // 4. Инициализация сессии libssh2 (глобальная init больше не нужна)
    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) {
        qDebug() << "Session init failed";
        close(sock);
        return;
    }

    if (libssh2_session_handshake(session, sock)) {
        qDebug() << "SSH handshake failed";
        libssh2_session_free(session);
        close(sock);
        return;
    }

    // 5. Аутентификация по паролю
    if (libssh2_userauth_password(session,
                                  m_user.toStdString().c_str(),
                                  m_password.toStdString().c_str())) {
        qDebug() << "Authentication failed";
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        close(sock);
        return;
    }

    // 6. Инициализация SFTP
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    if (!sftp) {
        qDebug() << "SFTP init failed";
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        close(sock);
        return;
    }

    // 7. Открываем удалённый файл для записи
    LIBSSH2_SFTP_HANDLE *sftpHandle = libssh2_sftp_open(sftp,
        m_remoteFile.toStdString().c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);

    if (!sftpHandle) {
        qDebug() << "SFTP open failed:" << libssh2_session_last_error(session, nullptr, nullptr, 0);
        libssh2_sftp_shutdown(sftp);
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        close(sock);
        return;
    }

    // 8. Читаем локальный файл
    QFile file(m_localFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open local file:" << m_localFile;
        libssh2_sftp_close(sftpHandle);
        libssh2_sftp_shutdown(sftp);
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        close(sock);
        return;
    }
    QByteArray data = file.readAll();

    // 9. Отправляем данные
    ssize_t written = libssh2_sftp_write(sftpHandle, data.constData(), data.size());
    if (written < data.size()) {
        qDebug() << "SFTP write incomplete";
    }

    // 10. Закрываем всё
    libssh2_sftp_close(sftpHandle);
    libssh2_sftp_shutdown(sftp);
    libssh2_session_disconnect(session, "Bye");
    libssh2_session_free(session);
    close(sock);

    // LIBSSH2_exit() удалён — он устарел и не нужен в современных версиях

    qDebug() << "File uploaded successfully:" << m_remoteFile;
}

// ------------------ TaskQueue ------------------
TaskQueue::TaskQueue(QObject* parent) : QObject(parent) {}

void TaskQueue::addTask(Task* task) {
    m_queue.enqueue(task);
    QObject::connect(task, &Task::finished, this, &TaskQueue::next);
}

void TaskQueue::start() {
    next();
}

void TaskQueue::next() {
    if (m_queue.isEmpty()) return;
    Task* task = m_queue.dequeue();
    task->execute();
}

void DelayTask::execute()
{
    QTimer::singleShot(m_milliseconds, this, &Task::finished);
}