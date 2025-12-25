#pragma once

#include <QObject>
#include <QQueue>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// FreeBSD / POSIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

// libssh2
#include <libssh2.h>
#include <libssh2_sftp.h>

// ------------------ Базовая задача ------------------
class Task : public QObject
{
    Q_OBJECT
public:
    explicit Task(QObject *parent = nullptr)
        : QObject(parent)
    {}

    virtual void execute() = 0;

signals:
    void finished();
};


// ------------------ JSON-RPC задача ------------------
class JsonRpcTask : public Task
{
public:
    JsonRpcTask(const QUrl &url, const QJsonObject &json);

    void execute() override;

private:
    QUrl m_url;
    QJsonObject m_json;
};

// ------------------ SFTP задача ------------------
class SftpTask : public Task
{
public:
    SftpTask(const QString& host, int port,
             const QString& user, const QString& password,
             const QString& localFile, const QString& remoteFile);

    void execute() override;

private:
    QString m_host;
    int m_port;
    QString m_user;
    QString m_password;
    QString m_localFile;
    QString m_remoteFile;

    void uploadFile();
};

// ------------------ Очередь задач ------------------
class TaskQueue : public QObject
{
    Q_OBJECT
public:
    explicit TaskQueue(QObject* parent = nullptr);

    void addTask(Task* task);
    void start();

private slots:
    void next();

private:
    QQueue<Task*> m_queue;
};
// ------------------ Задача задержки ------------------
class DelayTask : public Task
{
    Q_OBJECT
public:
    explicit DelayTask(int milliseconds, QObject *parent = nullptr)
        : Task(parent), m_milliseconds(milliseconds)
    {}

    void execute() override;

private:
    int m_milliseconds;
};
