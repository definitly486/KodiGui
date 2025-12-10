// playerwindow.cpp — АБСОЛЮТНО ФИНАЛЬНАЯ ВЕРСИЯ (FreeBSD + Qt6, декабрь 2025)
#include "playerwindow.h"
#include "ui_playerwindow.h"
#include "m3u8interceptor.h"

#include <QWebEngineView>
#include <QWebEnginePage>                 // ← теперь используем стандартный page
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineFullScreenRequest>
#include <QFile>
#include <QTimer>
#include <QProcess>
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
    QWebEngineProfile *profile = new QWebEngineProfile("LiveTVProfile", this);
    QWebEnginePage *page = new QWebEnginePage(profile, this);  // ← обычный QWebEnginePage

    profile->setHttpUserAgent(
        "Mozilla/5.0 (X11; FreeBSD amd64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/129.0 Safari/537.36"
    );

    webView->setPage(page);
    setCentralWidget(webView);

    // Настройки
    auto *settings = page->settings();
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);

    // StreamDetector (для красивых логов)
    QFile scriptFile(":/stream-detector.js");
    if (scriptFile.open(QIODevice::ReadOnly)) {
        QString jsCode = scriptFile.readAll();
        scriptFile.close();

        QWebEngineScript script;
        script.setName("StreamDetector");
        script.setSourceCode(jsCode);
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setRunsOnSubFrames(true);
        script.setWorldId(QWebEngineScript::MainWorld);

        profile->scripts()->insert(script);
        qDebug() << "StreamDetector внедрён";
    }

    // === NETWORK INTERCEPTOR — ловит любой m3u8 на livetv869.me ===
    M3u8Interceptor *interceptor = new M3u8Interceptor(this);
    profile->setUrlRequestInterceptor(interceptor);

    connect(interceptor, &M3u8Interceptor::streamFound, this, [this](const QString &url) {
        if (m_mpvLaunched || !url.contains(".m3u8")) return;
        m_mpvLaunched = true;

          // Отправляем сигнал с захваченным URL
        emit urlCaptured(url); // посылаем URL назад в MainWindow

        qDebug().noquote() << "ЗАПУСКАЕМ MPV →" << url;

        QString mpvPath = QStandardPaths::findExecutable("mpv");
        if (mpvPath.isEmpty() || !QFile::exists(mpvPath))
            mpvPath = "/usr/local/bin/mpv";

        if (!QFile::exists(mpvPath)) {
            qCritical() << "mpv не найден! Выполните: pkg install mpv";
            return;
        }

        QProcess::startDetached(mpvPath, QStringList()
            << "--http-header-fields=Referer: https://livetv869.me/"
            << "--user-agent=Mozilla/5.0 (X11; FreeBSD amd64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0 Safari/537.36"
            << "--hwdec=auto-safe"
            << "--vo=gpu"
            << "--cache=yes"
            << "--volume=80"
            << "--no-terminal"
            << "--really-quiet"
            << "--title=LiveTV • NHL"
            << url
        );

        qDebug() << "mpv запущен!";
        QTimer::singleShot(800, this, &QWidget::close);
    });

    // Полноэкранный режим
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
setAttribute(Qt::WA_DontShowOnScreen);
hide();

}

PlayerWindow::~PlayerWindow()
{
    webView->close();
    webView->deleteLater();
    delete ui;
}


