// playerwindow.cpp — АБСОЛЮТНО ФИНАЛЬНАЯ ВЕРСИЯ (FreeBSD + Qt6, декабрь 2025)
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
#include <QMessageBox>
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

    // ... ваш существующий код, где создается webView ...
    webView = new QWebEngineView(this);
    
    // === ИНИЦИАЛИЗАЦИЯ WIDEVINE ===
    m_interceptor = new M3u8Interceptor(this);
    if (webView && webView->page()) {
        webView->page()->profile()->setUrlRequestInterceptor(m_interceptor);
    }
    setupWidevineCapture();
    // ==============================
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

// ============================================================================
// WIDEVINE INTEGRATION METHODS (для PlayerWindow)
// ============================================================================

void PlayerWindow::setupWidevineCapture()
{
    QString jsCode;
    QFile file("widevine_capture.js"); 
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        jsCode = file.readAll();
        file.close();
    } else {
        qDebug() << "Warning: widevine_capture.js not found in app dir!";
        return;
    }

    QWebEngineScript script;
    script.setSourceCode(jsCode);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    
    if (webView && webView->page()) {
        webView->page()->scripts().insert(script);
        qDebug() << "Widevine JS capture script injected into PlayerWindow.";
    }
}

// Вызовите эту функцию из вашей кнопки "Извлечь ключ" в PlayerWindow
// (Если кнопка в MainWindow, вам нужно будет передать сигнал в PlayerWindow)


// УБЕРИТЕ "on_" и "_clicked", так как это не слот кнопки внутри PlayerWindow, а метод, вызываемый извне
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
        if (!error.isEmpty()) qDebug() << "Python StdErr:" << error;
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
