// playerwindow.cpp — Widevine-ready (FreeBSD + Qt6)
#include "playerwindow.h"
#include "ui_playerwindow.h"
#include "m3u8interceptor.h"
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineProfile>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineFullScreenRequest>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>

PlayerWindow::PlayerWindow(const QUrl &url, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PlayerWindow)
    , m_mpvLaunched(false)
{
    ui->setupUi(this);
    setWindowTitle("LiveTV Player — Stream Detector Active");
    resize(1280, 800);

    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    webView = new QWebEngineView(this);

    // Один профиль + одна страница. Inject и interceptor — только после setPage.
    QWebEngineProfile *profile = new QWebEngineProfile("LiveTVProfile", this);
    profile->setHttpUserAgent(
        "Mozilla/5.0 (X11; FreeBSD amd64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/129.0 Safari/537.36"
    );

    QWebEnginePage *page = new QWebEnginePage(profile, this);
    webView->setPage(page);
    setCentralWidget(webView);

    auto *settings = page->settings();
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);

    // Один interceptor на активный профиль — его же читает extractCurrentKey()
    m_interceptor = new M3u8Interceptor(this);
    profile->setUrlRequestInterceptor(m_interceptor);

    connect(m_interceptor, &M3u8Interceptor::streamFound, this, [this](const QString &streamUrl) {
        if (m_mpvLaunched || !streamUrl.contains(".m3u8"))
            return;
        m_mpvLaunched = true;
        emit urlCaptured(streamUrl);
    });

    // Widevine JS — ПОСЛЕ setPage, в тот же profile
    setupWidevineCapture();

    // StreamDetector (опционально, из ресурсов)
    QFile scriptFile(":/stream-detector.js");
    if (scriptFile.open(QIODevice::ReadOnly)) {
        QWebEngineScript script;
        script.setName("StreamDetector");
        script.setSourceCode(scriptFile.readAll());
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setRunsOnSubFrames(true);
        script.setWorldId(QWebEngineScript::MainWorld);
        profile->scripts()->insert(script);
        qDebug() << "StreamDetector внедрён";
        scriptFile.close();
    }

    connect(page, &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest r) {
        r.accept();
        r.toggleOn() ? showFullScreen() : showNormal();
    });

    connect(page, &QWebEnginePage::loadFinished, this, [](bool ok) {
        qDebug() << (ok ? "Страница загружена — ждём поток..." : "Ошибка загрузки");
    });

    if (url.isValid()) {
        qDebug() << "Загружаем:" << url.toString();
        webView->load(url);
    }
    // Окно должно быть видимым — не ставить WA_DontShowOnScreen / hide()
}

PlayerWindow::~PlayerWindow()
{
    if (webView) {
        webView->close();
        webView->deleteLater();
        webView = nullptr;
    }
    delete ui;
}

// ============================================================================
// WIDEVINE INTEGRATION METHODS (для PlayerWindow)
// ============================================================================

void PlayerWindow::setupWidevineCapture()
{
    // Ищем widevine_capture.js рядом с бинарником и в cwd
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/widevine_capture.js",
        QDir::currentPath() + "/widevine_capture.js",
        QStringLiteral("widevine_capture.js")
    };

    QFile file;
    bool opened = false;
    for (const QString &path : candidates) {
        file.setFileName(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            opened = true;
            qDebug() << "widevine_capture.js loaded from:" << path;
            break;
        }
    }
    if (!opened) {
        qDebug() << "Warning: widevine_capture.js not found! Tried:" << candidates;
        return;
    }

    const QString jsCode = QString::fromUtf8(file.readAll());
    file.close();

    QWebEngineScript script;
    script.setName(QStringLiteral("WidevineCapture"));
    script.setSourceCode(jsCode);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setRunsOnSubFrames(true);
    script.setWorldId(QWebEngineScript::MainWorld);

    if (webView && webView->page() && webView->page()->profile()) {
        webView->page()->profile()->scripts()->insert(script);
        qDebug() << "Widevine JS capture script injected into profile.";
    } else {
        qDebug() << "Error: cannot inject Widevine script — no page/profile";
    }
}

void PlayerWindow::extractCurrentKey()
{
    if (!webView || !webView->page() || !m_interceptor) {
        qDebug() << "Error: WebView or Interceptor not initialized in PlayerWindow!";
        return;
    }

    webView->page()->runJavaScript("localStorage.getItem('wv_last_pssh');",
        [this](const QVariant &result) {
            QString pssh = result.toString();
            if (pssh.isEmpty()) {
                qDebug() << "PSSH not found. Play DRM content first.";
                return;
            }

            auto licReq = m_interceptor->getLastLicenseRequest();
            if (!licReq.isValid) {
                qDebug() << "License request not intercepted.";
                return;
            }

            extractKeyWithPython(pssh, licReq.url, licReq.headers);
        });
}

void PlayerWindow::extractKeyWithPython(const QString &pssh, const QString &licUrl, const QString &licHeaders)
{
    QProcess *pythonProcess = new QProcess(this);
    QString appDir = QCoreApplication::applicationDirPath();
    QString scriptPath = appDir + "/drm_tools/extract_key.py";
    QString devicePath = appDir + "/drm_tools/device.wvd";

    QStringList arguments;
    arguments << scriptPath << pssh << licUrl << licHeaders << devicePath;

    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [=]() {
        QString output = pythonProcess->readAllStandardOutput();
        qDebug() << "Python Output:" << output;

        if (output.contains("KEY_FOUND:")) {
            QStringList lines = output.split('\n');
            for (const QString &line : lines) {
                if (line.startsWith("KEY_FOUND:")) {
                    QString keyData = line.mid(10);
                    qDebug() << "SUCCESS! Key:" << keyData;
                    saveKeyToFile(keyData, pssh, licUrl);
                    QMessageBox::information(this, "Успех", "Ключ получен:\n" + keyData);
                }
            }
        } else if (output.contains("ERROR_")) {
            qDebug() << "Python Error:" << output;
            QMessageBox::critical(this, "Ошибка", output);
        }
    });

    connect(pythonProcess, &QProcess::readyReadStandardError, this, [=]() {
        QString error = pythonProcess->readAllStandardError();
        if (!error.isEmpty())
            qDebug() << "Python StdErr:" << error;
    });

    pythonProcess->start("python3", arguments);
}

void PlayerWindow::saveKeyToFile(const QString &key, const QString &pssh, const QString &licUrl)
{
    QString fileName = QCoreApplication::applicationDirPath() + "/widevine_keys.txt";
    QFile file(fileName);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << "========================================\n";
        out << "Date: " << QDateTime::currentDateTime().toString() << "\n";
        out << "Stream/License URL: " << licUrl << "\n";
        out << "PSSH: " << pssh << "\n";
        out << "KEY: " << key << "\n";
        out << "========================================\n\n";
        file.close();
        qDebug() << "Key saved to:" << fileName;
    }
}
